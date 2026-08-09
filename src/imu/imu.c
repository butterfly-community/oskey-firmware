/* SPDX-License-Identifier: Apache-2.0 */

#include "imu.h"

#include <errno.h>
#include <math.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zsl/orientation/fusion/aqua.h>
#include <zsl/vectors.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_SAMPLE_FREQUENCY_HZ DT_PROP(IMU_NODE, accel_odr)
#define IMU_SAMPLE_PERIOD_US    DIV_ROUND_CLOSEST(USEC_PER_SEC, IMU_SAMPLE_FREQUENCY_HZ)
#define IMU_STANDARD_GRAVITY    9.80665f
#define IMU_INITIAL_ACCEL_ERROR (0.20f * IMU_STANDARD_GRAVITY)
#define IMU_AQUA_ACCEL_GAIN     0.02f

BUILD_ASSERT(DT_PROP(IMU_NODE, accel_odr) == DT_PROP(IMU_NODE, gyro_odr),
	     "accelerometer and gyroscope ODR must match for fusion");

static const struct device *imu_dev;
static enum app_imu_state imu_state = APP_IMU_DISABLED;
static atomic_t imu_streaming;

struct imu_fusion {
	struct zsl_fus_aqua_cfg config;
	struct zsl_quat quaternion;
	bool initialized;
};

K_SEM_DEFINE(imu_run_sem, 0, 1);

static void imu_publish_state(enum app_imu_state state)
{
	if (state == imu_state) {
		return;
	}
	imu_state = state;
	(void)zbus_chan_pub(&app_imu_state_chan, &state, K_MSEC(100));
}

static int imu_publish_sample(struct imu_fusion *fusion)
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
	float accel_norm = zsl_vec_norm(&accel);

	if (!fusion->initialized &&
	    (!isfinite(accel_norm) ||
	     fabsf(accel_norm - IMU_STANDARD_GRAVITY) > IMU_INITIAL_ACCEL_ERROR)) {
		goto publish;
	}

	ret = zsl_fus_aqua_feed(&accel, NULL, &gyro, NULL, &fusion->quaternion, &fusion->config);

	if (ret < 0) {
		LOG_ERR("IMU fusion failed: %d", ret);
		return ret;
	}

	float qw = fusion->quaternion.r;
	float qx = -fusion->quaternion.i;
	float qy = fusion->quaternion.j;
	float qz = fusion->quaternion.k;

	if (!isfinite(qw) || !isfinite(qx) || !isfinite(qy) || !isfinite(qz)) {
		fusion->quaternion = (struct zsl_quat){.r = 1.0f};
		fusion->initialized = false;
		goto publish;
	}

	sample.valid = true;
	sample.quaternion_w = qw;
	sample.quaternion_x = qx;
	sample.quaternion_y = qy;
	sample.quaternion_z = qz;
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

	k_timer_init(&sample_timer, NULL, NULL);

	imu_publish_state(APP_IMU_IDLE);

	while (true) {
		k_sem_take(&imu_run_sem, K_FOREVER);
		struct imu_fusion fusion = {
			.config =
				{
					.alpha = IMU_AQUA_ACCEL_GAIN,
					.e_a = 0.9f,
				},
			.quaternion = {.r = 1.0f},
		};
		int ret = zsl_fus_aqua_init(IMU_SAMPLE_FREQUENCY_HZ, &fusion.config);

		errors = 0;
		if (ret < 0) {
			LOG_ERR("IMU fusion initialization failed: %d", ret);
			imu_publish_state(APP_IMU_ERROR);
			atomic_set(&imu_streaming, 0);
			continue;
		}

		k_timer_start(&sample_timer, K_USEC(IMU_SAMPLE_PERIOD_US),
			      K_USEC(IMU_SAMPLE_PERIOD_US));

		while (atomic_get(&imu_streaming)) {
			ret = sensor_sample_fetch(imu_dev);

			if (ret == 0) {
				ret = imu_publish_sample(&fusion);
			}
			if (ret == 0) {
				errors = 0;
				imu_publish_state(APP_IMU_READY);
			} else {
				LOG_ERR("IMU sample fetch failed: %d", ret);
				errors++;
				if (errors >= 5) {
					imu_publish_state(APP_IMU_ERROR);
				}
			}

			(void)k_timer_status_sync(&sample_timer);
		}

		k_timer_stop(&sample_timer);
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
