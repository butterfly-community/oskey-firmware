/* SPDX-License-Identifier: Apache-2.0 */

#include "imu.h"

#include <Fusion.h>
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_SAMPLE_FREQUENCY_HZ DT_PROP(IMU_NODE, accel_odr)
#define IMU_SAMPLE_PERIOD_US    DIV_ROUND_CLOSEST(USEC_PER_SEC, IMU_SAMPLE_FREQUENCY_HZ)
#define IMU_COMMAND_QUEUE_DEPTH 4
#define IMU_MICRO_G_PER_G       1000000.0f
#define IMU_10UDEG_PER_DEGREE   100000.0f

BUILD_ASSERT(DT_PROP(IMU_NODE, accel_odr) == DT_PROP(IMU_NODE, gyro_odr),
	     "accelerometer and gyroscope ODR must match for fusion");

static const struct device *const imu_dev = DEVICE_DT_GET(IMU_NODE);
static enum app_imu_state imu_state = APP_IMU_DISABLED;

struct imu_runtime {
	FusionAhrs ahrs;
	FusionBias bias;
	int64_t last_sample_ticks;
	int errors;
	bool streaming;
};

K_MSGQ_DEFINE(imu_command_queue, sizeof(struct app_imu_command), IMU_COMMAND_QUEUE_DEPTH,
	      __alignof__(struct app_imu_command));

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

static void imu_fusion_initialize(struct imu_runtime *runtime)
{
	FusionAhrsInitialise(&runtime->ahrs);
	FusionAhrsSettings ahrs_settings = fusionAhrsDefaultSettings;

	ahrs_settings.sampleRate = IMU_SAMPLE_FREQUENCY_HZ;
	ahrs_settings.gyroscopeRange = DT_PROP(IMU_NODE, gyro_fs);
	ahrs_settings.accelerationRejection = 10.0f;
	ahrs_settings.magneticRejection = 0.0f;
	ahrs_settings.rejectionTimeout = 5.0f;
	FusionAhrsSetSettings(&runtime->ahrs, &ahrs_settings);

	FusionBiasInitialise(&runtime->bias);
	FusionBiasSettings bias_settings = fusionBiasDefaultSettings;

	bias_settings.sampleRate = IMU_SAMPLE_FREQUENCY_HZ;
	FusionBiasSetSettings(&runtime->bias, &bias_settings);
}

static FusionVector imu_accel_to_fusion(const struct sensor_value sensor[3])
{
	return (FusionVector){.axis = {
				      .x = (float)sensor_ms2_to_ug(&sensor[1]) / IMU_MICRO_G_PER_G,
				      .y = -(float)sensor_ms2_to_ug(&sensor[0]) / IMU_MICRO_G_PER_G,
				      .z = (float)sensor_ms2_to_ug(&sensor[2]) / IMU_MICRO_G_PER_G,
			      }};
}

static FusionVector imu_gyro_to_fusion(const struct sensor_value sensor[3])
{
	return (FusionVector){
		.axis = {
			.x = (float)sensor_rad_to_10udegrees(&sensor[1]) / IMU_10UDEG_PER_DEGREE,
			.y = -(float)sensor_rad_to_10udegrees(&sensor[0]) / IMU_10UDEG_PER_DEGREE,
			.z = (float)sensor_rad_to_10udegrees(&sensor[2]) / IMU_10UDEG_PER_DEGREE,
		}};
}

static void imu_quaternion_to_render_matrix(FusionQuaternion quaternion, float rotation[9])
{
	quaternion.element.y = -quaternion.element.y;
	quaternion.element.z = -quaternion.element.z;
	const FusionMatrix matrix = FusionQuaternionToMatrix(quaternion);

	memcpy(rotation, matrix.array, sizeof(matrix.array));
}

static int imu_process_sample(struct imu_runtime *runtime)
{
	struct sensor_value accel_sensor[3];
	struct sensor_value gyro_sensor[3];
	int ret = sensor_sample_fetch(imu_dev);

	if (ret < 0) {
		LOG_ERR("IMU sample fetch failed: %d", ret);
		return ret;
	}

	if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel_sensor) < 0 ||
	    sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro_sensor) < 0) {
		LOG_ERR("IMU channel read failed");
		return -EIO;
	}
	const int64_t sample_ticks = k_uptime_ticks();

	const FusionVector accel = imu_accel_to_fusion(accel_sensor);
	FusionVector gyro = imu_gyro_to_fusion(gyro_sensor);

	gyro = FusionBiasUpdate(&runtime->bias, gyro);
	if (runtime->last_sample_ticks != 0) {
		const int64_t elapsed_ticks = sample_ticks - runtime->last_sample_ticks;
		const float sample_period =
			(float)k_ticks_to_us_floor64(elapsed_ticks) / USEC_PER_SEC;

		FusionAhrsSetSamplePeriod(&runtime->ahrs, sample_period);
	}
	runtime->last_sample_ticks = sample_ticks;
	FusionAhrsUpdateNoMagnetometer(&runtime->ahrs, gyro, accel);

	struct app_imu_sample sample = {
		.valid = !FusionAhrsGetFlags(&runtime->ahrs).startup,
	};

	imu_quaternion_to_render_matrix(FusionAhrsGetQuaternion(&runtime->ahrs), sample.rotation);

	ret = zbus_chan_pub(&app_imu_sample_chan, &sample, K_MSEC(100));
	if (ret < 0) {
		LOG_ERR("IMU sample publish failed: %d", ret);
	}
	return ret;
}

static void imu_handle_command(const struct app_imu_command *command, struct imu_runtime *runtime)
{
	switch (command->kind) {
	case APP_IMU_COMMAND_START:
		if (!runtime->streaming) {
			FusionAhrsRestart(&runtime->ahrs);
			runtime->last_sample_ticks = 0;
			runtime->errors = 0;
			runtime->streaming = true;
			imu_publish_state(APP_IMU_INITIALIZING);
		}
		break;
	case APP_IMU_COMMAND_STOP:
		if (runtime->streaming) {
			runtime->streaming = false;
			imu_publish_state(APP_IMU_IDLE);
		}
		break;
	default:
		break;
	}
}

static void imu_command_listener(const struct zbus_channel *chan)
{
	const struct app_imu_command *command = zbus_chan_const_msg(chan);

	if (k_msgq_put(&imu_command_queue, command, K_NO_WAIT) < 0) {
		LOG_ERR("IMU command queue full");
	}
}

ZBUS_LISTENER_DEFINE(imu_command_listener_ob, imu_command_listener);
ZBUS_CHAN_ADD_OBS(app_imu_command_chan, imu_command_listener_ob, 0);

static void imu_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	struct imu_runtime runtime = {0};

	imu_fusion_initialize(&runtime);
	imu_publish_state(APP_IMU_IDLE);

	while (true) {
		struct app_imu_command command;
		const k_timeout_t timeout =
			runtime.streaming ? K_USEC(IMU_SAMPLE_PERIOD_US) : K_FOREVER;

		if (k_msgq_get(&imu_command_queue, &command, timeout) == 0) {
			imu_handle_command(&command, &runtime);
			continue;
		}

		int ret = imu_process_sample(&runtime);

		if (ret == 0) {
			runtime.errors = 0;
			imu_publish_state(FusionAhrsGetFlags(&runtime.ahrs).startup
						  ? APP_IMU_INITIALIZING
						  : APP_IMU_READY);
		} else {
			LOG_ERR("IMU sample processing failed: %d", ret);
			runtime.errors++;
			if (runtime.errors >= 5) {
				imu_publish_state(APP_IMU_ERROR);
			}
		}
	}
}

K_THREAD_DEFINE(imu_thread_id, CONFIG_OSKEY_IMU_THREAD_STACK_SIZE, imu_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(K_LOWEST_APPLICATION_THREAD_PRIO), 0, SYS_FOREVER_MS);

int app_imu_init(void)
{
	if (!device_is_ready(imu_dev)) {
		LOG_ERR("IMU device is not ready");
		imu_publish_state(APP_IMU_ERROR);
		return -ENODEV;
	}

	imu_publish_state(APP_IMU_INITIALIZING);
	k_thread_start(imu_thread_id);
	return 0;
}
