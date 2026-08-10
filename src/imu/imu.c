/* SPDX-License-Identifier: Apache-2.0 */

#include "imu.h"

#include <errno.h>
#include <math.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zsl/orientation/quaternions.h>
#include <zsl/vectors.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_SAMPLE_FREQUENCY_HZ     DT_PROP(IMU_NODE, accel_odr)
#define IMU_SAMPLE_PERIOD_US        DIV_ROUND_CLOSEST(USEC_PER_SEC, IMU_SAMPLE_FREQUENCY_HZ)
#define IMU_INITIAL_ACCEL_ERROR     ((zsl_real_t)0.20 * (zsl_real_t)ZSL_GRAV_EARTH)
#define IMU_CALIBRATION_ACCEL_ERROR ((zsl_real_t)0.10 * (zsl_real_t)ZSL_GRAV_EARTH)
#define IMU_CALIBRATION_GYRO_LIMIT  ((zsl_real_t)5.0 * (zsl_real_t)ZSL_DEG_TO_RAD)
#define IMU_CALIBRATION_SAMPLES     (3 * IMU_SAMPLE_FREQUENCY_HZ)
#define IMU_MADGWICK_GAIN           0.1f
#define IMU_NORM_EPSILON            1.0e-6f
#define IMU_ANTIPARALLEL_LIMIT      (-0.9999f)

BUILD_ASSERT(DT_PROP(IMU_NODE, accel_odr) == DT_PROP(IMU_NODE, gyro_odr),
	     "accelerometer and gyroscope ODR must match for fusion");

static const struct device *imu_dev;
static enum app_imu_state imu_state = APP_IMU_DISABLED;
static atomic_t imu_streaming;
static atomic_t imu_calibration_requested;

struct imu_fusion {
	struct zsl_quat quaternion;
	bool initialized;
};

enum imu_calibration_state {
	IMU_CALIBRATION_REQUIRED,
	IMU_CALIBRATION_ACTIVE,
	IMU_CALIBRATION_READY,
};

struct imu_gyro_calibration {
	zsl_real_t bias[3];
	zsl_real_t sum[3];
	uint32_t samples;
	enum imu_calibration_state state;
};

K_SEM_DEFINE(imu_run_sem, 0, 1);

static void imu_publish_state(enum app_imu_state state)
{
	if (state == imu_state) {
		return;
	}
	int ret = zbus_chan_pub(&app_imu_state_chan, &state, K_MSEC(100));

	if (ret < 0) {
		LOG_ERR("IMU state publish failed: %d", ret);
		return;
	}
	imu_state = state;
}

static void imu_fusion_initialize(struct imu_fusion *fusion, struct zsl_vec *accel)
{
	zsl_real_t norm = zsl_vec_norm(accel);
	zsl_real_t x = accel->data[0] / norm;
	zsl_real_t y = accel->data[1] / norm;
	zsl_real_t z = accel->data[2] / norm;

	if (z < IMU_ANTIPARALLEL_LIMIT) {
		fusion->quaternion = (struct zsl_quat){.i = 1.0f};
	} else {
		fusion->quaternion = (struct zsl_quat){
			.r = 1.0f + z,
			.i = y,
			.j = -x,
		};
		(void)zsl_quat_to_unit_d(&fusion->quaternion);
	}
}

static int imu_madgwick_feed(struct imu_fusion *fusion, struct zsl_vec *accel, struct zsl_vec *gyro)
{
	struct zsl_quat *q = &fusion->quaternion;
	zsl_real_t quaternion_norm = zsl_quat_magn(q);

	if (!isfinite(quaternion_norm) || quaternion_norm < IMU_NORM_EPSILON) {
		return -ERANGE;
	}
	(void)zsl_quat_to_unit_d(q);

	struct zsl_quat angular_velocity = {
		.i = gyro->data[0],
		.j = gyro->data[1],
		.k = gyro->data[2],
	};
	struct zsl_quat derivative;
	(void)zsl_quat_mult(q, &angular_velocity, &derivative);
	(void)zsl_quat_scale_d(&derivative, 0.5f);

	zsl_real_t gradient[4] = {0};
	zsl_real_t accel_norm = zsl_vec_norm(accel);

	if (isfinite(accel_norm) && accel_norm > IMU_NORM_EPSILON) {
		(void)zsl_vec_to_unit(accel);
		zsl_real_t f[3] = {
			2.0f * (q->i * q->k - q->r * q->j) - accel->data[0],
			2.0f * (q->r * q->i + q->j * q->k) - accel->data[1],
			1.0f - 2.0f * (q->i * q->i + q->j * q->j) - accel->data[2],
		};

		gradient[0] = -2.0f * q->j * f[0] + 2.0f * q->i * f[1];
		gradient[1] = 2.0f * q->k * f[0] + 2.0f * q->r * f[1] - 4.0f * q->i * f[2];
		gradient[2] = -2.0f * q->r * f[0] + 2.0f * q->k * f[1] - 4.0f * q->j * f[2];
		gradient[3] = 2.0f * q->i * f[0] + 2.0f * q->j * f[1];

		zsl_real_t gradient_norm =
			ZSL_SQRT(gradient[0] * gradient[0] + gradient[1] * gradient[1] +
				 gradient[2] * gradient[2] + gradient[3] * gradient[3]);
		if (isfinite(gradient_norm) && gradient_norm > IMU_NORM_EPSILON) {
			for (size_t i = 0; i < ARRAY_SIZE(gradient); i++) {
				gradient[i] /= gradient_norm;
			}
		} else {
			for (size_t i = 0; i < ARRAY_SIZE(gradient); i++) {
				gradient[i] = 0.0f;
			}
		}
	}

	zsl_real_t dt = 1.0f / IMU_SAMPLE_FREQUENCY_HZ;
	q->r += (derivative.r - IMU_MADGWICK_GAIN * gradient[0]) * dt;
	q->i += (derivative.i - IMU_MADGWICK_GAIN * gradient[1]) * dt;
	q->j += (derivative.j - IMU_MADGWICK_GAIN * gradient[2]) * dt;
	q->k += (derivative.k - IMU_MADGWICK_GAIN * gradient[3]) * dt;
	return zsl_quat_to_unit_d(q);
}

static void imu_fusion_reset(struct imu_fusion *fusion)
{
	fusion->quaternion = (struct zsl_quat){.r = 1.0f};
	fusion->initialized = false;
}

static void imu_calibration_start(struct imu_gyro_calibration *calibration)
{
	*calibration = (struct imu_gyro_calibration){.state = IMU_CALIBRATION_ACTIVE};
}

static void imu_calibration_cancel(struct imu_gyro_calibration *calibration)
{
	if (calibration->state == IMU_CALIBRATION_ACTIVE) {
		*calibration = (struct imu_gyro_calibration){
			.state = IMU_CALIBRATION_REQUIRED,
		};
	}
}

static void imu_calibration_feed(struct imu_gyro_calibration *calibration, struct zsl_vec *accel,
				 struct zsl_vec *gyro)
{
	zsl_real_t accel_norm = zsl_vec_norm(accel);
	zsl_real_t gyro_norm = zsl_vec_norm(gyro);

	if (!isfinite(accel_norm) || !isfinite(gyro_norm) ||
	    ZSL_ABS(accel_norm - (zsl_real_t)ZSL_GRAV_EARTH) > IMU_CALIBRATION_ACCEL_ERROR ||
	    gyro_norm > IMU_CALIBRATION_GYRO_LIMIT) {
		imu_calibration_start(calibration);
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(calibration->sum); i++) {
		calibration->sum[i] += gyro->data[i];
	}
	calibration->samples++;
	if (calibration->samples < IMU_CALIBRATION_SAMPLES) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(calibration->bias); i++) {
		calibration->bias[i] = calibration->sum[i] / calibration->samples;
	}
	calibration->state = IMU_CALIBRATION_READY;
	LOG_INF("Gyroscope calibration complete");
}

static enum app_imu_state imu_active_state(const struct imu_fusion *fusion,
					   const struct imu_gyro_calibration *calibration)
{
	switch (calibration->state) {
	case IMU_CALIBRATION_ACTIVE:
		return APP_IMU_CALIBRATING;
	case IMU_CALIBRATION_READY:
		return fusion->initialized ? APP_IMU_READY : APP_IMU_INITIALIZING;
	case IMU_CALIBRATION_REQUIRED:
	default:
		return APP_IMU_UNCALIBRATED;
	}
}

static int imu_publish_sample(struct imu_fusion *fusion, struct imu_gyro_calibration *calibration)
{
	struct sensor_value accel_sensor[3];
	struct sensor_value gyro_sensor[3];
	int ret;

	if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel_sensor) < 0 ||
	    sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro_sensor) < 0) {
		LOG_ERR("IMU channel read failed");
		return -EIO;
	}

	ZSL_VECTOR_DEF(accel, 3);
	ZSL_VECTOR_DEF(gyro, 3);

	accel.data[0] = sensor_value_to_float(&accel_sensor[1]);
	accel.data[1] = -sensor_value_to_float(&accel_sensor[0]);
	accel.data[2] = sensor_value_to_float(&accel_sensor[2]);
	gyro.data[0] = sensor_value_to_float(&gyro_sensor[1]);
	gyro.data[1] = -sensor_value_to_float(&gyro_sensor[0]);
	gyro.data[2] = sensor_value_to_float(&gyro_sensor[2]);

	struct app_imu_sample sample = {.quaternion_w = 1.0f};

	if (calibration->state == IMU_CALIBRATION_ACTIVE) {
		imu_calibration_feed(calibration, &accel, &gyro);
	}
	if (calibration->state != IMU_CALIBRATION_READY) {
		goto publish;
	}
	for (size_t i = 0; i < ARRAY_SIZE(calibration->bias); i++) {
		gyro.data[i] -= calibration->bias[i];
	}

	zsl_real_t accel_norm = zsl_vec_norm(&accel);

	if (!fusion->initialized &&
	    (!isfinite(accel_norm) ||
	     ZSL_ABS(accel_norm - (zsl_real_t)ZSL_GRAV_EARTH) > IMU_INITIAL_ACCEL_ERROR)) {
		goto publish;
	}

	if (!fusion->initialized) {
		imu_fusion_initialize(fusion, &accel);
	} else {
		ret = imu_madgwick_feed(fusion, &accel, &gyro);
		if (ret < 0) {
			LOG_ERR("IMU fusion failed: %d", ret);
			imu_fusion_reset(fusion);
			goto publish;
		}
	}

	const struct zsl_quat *q = &fusion->quaternion;
	if (!isfinite(q->r) || !isfinite(q->i) || !isfinite(q->j) || !isfinite(q->k)) {
		imu_fusion_reset(fusion);
		goto publish;
	}

	sample = (struct app_imu_sample){
		.valid = true,
		.quaternion_w = q->r,
		.quaternion_x = q->i,
		.quaternion_y = -q->j,
		.quaternion_z = -q->k,
	};
	fusion->initialized = true;

publish:
	ret = zbus_chan_pub(&app_imu_sample_chan, &sample, K_MSEC(100));
	if (ret < 0) {
		LOG_ERR("IMU sample publish failed: %d", ret);
	}
	return ret;
}

static void imu_start(void)
{
	if (imu_dev == NULL || !device_is_ready(imu_dev)) {
		imu_publish_state(APP_IMU_ERROR);
		return;
	}
	if (atomic_cas(&imu_streaming, 0, 1)) {
		k_sem_give(&imu_run_sem);
	}
}

static void imu_stop(void)
{
	atomic_set(&imu_calibration_requested, 0);
	atomic_set(&imu_streaming, 0);
}

static void imu_command_listener(const struct zbus_channel *chan)
{
	const struct app_imu_command *command = zbus_chan_const_msg(chan);

	switch (command->kind) {
	case APP_IMU_COMMAND_START:
		imu_start();
		break;
	case APP_IMU_COMMAND_STOP:
		imu_stop();
		break;
	case APP_IMU_COMMAND_CALIBRATE:
		atomic_set(&imu_calibration_requested, 1);
		imu_start();
		break;
	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(imu_command_listener_ob, imu_command_listener);
ZBUS_CHAN_ADD_OBS(app_imu_command_chan, imu_command_listener_ob, 0);

static void imu_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	int errors = 0;
	struct k_timer sample_timer;
	struct imu_gyro_calibration calibration = {0};

	k_timer_init(&sample_timer, NULL, NULL);

	imu_publish_state(APP_IMU_IDLE);

	while (true) {
		k_sem_take(&imu_run_sem, K_FOREVER);
		struct imu_fusion fusion;
		imu_fusion_reset(&fusion);
		int ret;

		errors = 0;
		k_timer_start(&sample_timer, K_USEC(IMU_SAMPLE_PERIOD_US),
			      K_USEC(IMU_SAMPLE_PERIOD_US));
		imu_publish_state(imu_active_state(&fusion, &calibration));

		while (atomic_get(&imu_streaming)) {
			if (atomic_cas(&imu_calibration_requested, 1, 0)) {
				imu_calibration_start(&calibration);
				imu_fusion_reset(&fusion);
				imu_publish_state(APP_IMU_CALIBRATING);
			}

			ret = sensor_sample_fetch(imu_dev);

			if (ret == 0) {
				ret = imu_publish_sample(&fusion, &calibration);
			}
			if (ret == 0) {
				errors = 0;
				imu_publish_state(imu_active_state(&fusion, &calibration));
			} else {
				LOG_ERR("IMU sample processing failed: %d", ret);
				errors++;
				if (errors >= 5) {
					imu_publish_state(APP_IMU_ERROR);
				}
			}

			(void)k_timer_status_sync(&sample_timer);
		}

		k_timer_stop(&sample_timer);
		imu_calibration_cancel(&calibration);
		imu_publish_state(APP_IMU_IDLE);
	}
}

K_THREAD_DEFINE(imu_thread_id, CONFIG_OSKEY_IMU_THREAD_STACK_SIZE, imu_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(K_LOWEST_APPLICATION_THREAD_PRIO), 0, SYS_FOREVER_MS);

int app_imu_init(void)
{
	imu_dev = DEVICE_DT_GET(IMU_NODE);
	if (!device_is_ready(imu_dev)) {
		LOG_ERR("IMU device is not ready");
		imu_publish_state(APP_IMU_ERROR);
		return -ENODEV;
	}

	imu_publish_state(APP_IMU_INITIALIZING);
	k_thread_start(imu_thread_id);
	return 0;
}
