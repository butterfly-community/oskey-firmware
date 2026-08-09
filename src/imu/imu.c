/* SPDX-License-Identifier: Apache-2.0 */

#include "imu.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "bus.h"
#include "imu_orientation.h"

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_SAMPLE_FREQUENCY_HZ DT_PROP(IMU_NODE, accel_odr)
#define IMU_SAMPLE_PERIOD_US    DIV_ROUND_CLOSEST(USEC_PER_SEC, IMU_SAMPLE_FREQUENCY_HZ)

BUILD_ASSERT(DT_PROP(IMU_NODE, accel_odr) == DT_PROP(IMU_NODE, gyro_odr),
	     "accelerometer and gyroscope ODR must match for fusion");

static const struct device *imu_dev;
static enum app_imu_state imu_state = APP_IMU_DISABLED;
static atomic_t imu_streaming;

K_SEM_DEFINE(imu_run_sem, 0, 1);

static void imu_publish_state(enum app_imu_state state)
{
	if (state == imu_state) {
		return;
	}
	imu_state = state;
	(void)zbus_chan_pub(&app_imu_state_chan, &state, K_MSEC(100));
}

static int imu_publish_sample(struct app_imu_orientation *orientation)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];

	if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel) < 0 ||
	    sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro) < 0) {
		LOG_ERR("IMU channel read failed");
		return -EIO;
	}

	float accel_sensor[3];
	float gyro_sensor[3];

	for (size_t i = 0; i < 3; i++) {
		accel_sensor[i] = sensor_value_to_float(&accel[i]);
		gyro_sensor[i] = sensor_value_to_float(&gyro[i]);
	}

	struct app_imu_orientation_result fused;
	int ret = app_imu_orientation_update(orientation, accel_sensor, gyro_sensor, &fused);

	if (ret < 0) {
		LOG_ERR("IMU fusion failed: %d", ret);
		return ret;
	}

	struct app_imu_sample sample = {
		.valid = fused.valid,
		.quaternion_w = fused.valid ? fused.quaternion_w : 1.0f,
		.quaternion_x = fused.quaternion_x,
		.quaternion_y = fused.quaternion_y,
		.quaternion_z = fused.quaternion_z,
	};

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
		struct app_imu_orientation orientation;
		int ret = app_imu_orientation_init(&orientation, IMU_SAMPLE_FREQUENCY_HZ);

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
				ret = imu_publish_sample(&orientation);
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
