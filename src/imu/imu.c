/* SPDX-License-Identifier: Apache-2.0 */

#include "imu.h"

#include <math.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_RAD_TO_DEG (180.0f / 3.14159265358979323846f)

static const struct device *imu_dev;
static enum app_imu_state imu_state = APP_IMU_DISABLED;

static void imu_publish_state(enum app_imu_state state)
{
	if (state == imu_state) {
		return;
	}
	imu_state = state;
	(void)zbus_chan_pub(&app_imu_state_chan, &state, K_MSEC(100));
}

static void imu_publish_sample(void)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];

	if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel) < 0 ||
	    sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro) < 0) {
		LOG_ERR("IMU channel read failed");
		return;
	}

	float ax = sensor_value_to_float(&accel[0]);
	float ay = sensor_value_to_float(&accel[1]);
	float az = sensor_value_to_float(&accel[2]);

	/* Tilt angles from the accelerometer, matching the board reference
	 * formulas: angle = atan(axis / sqrt(other two axes squared)).
	 */
	float pitch = atanf(ax / sqrtf(ay * ay + az * az)) * IMU_RAD_TO_DEG;
	float roll = atanf(ay / sqrtf(ax * ax + az * az)) * IMU_RAD_TO_DEG;

	struct app_imu_sample sample = {
		.pitch = pitch,
		.roll = roll,
		.gyro_x = sensor_value_to_float(&gyro[0]) * IMU_RAD_TO_DEG,
		.gyro_y = sensor_value_to_float(&gyro[1]) * IMU_RAD_TO_DEG,
		.gyro_z = sensor_value_to_float(&gyro[2]) * IMU_RAD_TO_DEG,
	};

	(void)zbus_chan_pub(&app_imu_sample_chan, &sample, K_MSEC(100));
}

static void imu_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	int errors = 0;

	while (true) {
		int ret = sensor_sample_fetch(imu_dev);

		if (ret == 0) {
			errors = 0;
			imu_publish_state(APP_IMU_READY);
			imu_publish_sample();
		} else {
			LOG_ERR("IMU sample fetch failed: %d", ret);
			errors++;
			if (errors >= 5) {
				imu_publish_state(APP_IMU_ERROR);
			}
		}

		k_sleep(K_MSEC(CONFIG_OSKEY_IMU_SAMPLE_INTERVAL_MS));
	}
}

K_THREAD_DEFINE(imu_thread_id, CONFIG_OSKEY_IMU_THREAD_STACK_SIZE, imu_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(7), 0, SYS_FOREVER_MS);

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
