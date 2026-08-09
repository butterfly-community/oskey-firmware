/* SPDX-License-Identifier: Apache-2.0 */

#ifndef OSKEY_IMU_ORIENTATION_H
#define OSKEY_IMU_ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>

#include <zsl/orientation/fusion/aqua.h>

struct app_imu_orientation {
	struct zsl_fus_aqua_cfg config;
	struct zsl_quat quaternion;
	float gyro_bias[3];
	float gyro_bias_sum[3];
	uint16_t gyro_bias_samples;
	bool gyro_bias_ready;
	bool initialized;
};

struct app_imu_orientation_result {
	float tilt;
	float direction;
	float quaternion_w;
	float quaternion_x;
	float quaternion_y;
	float quaternion_z;
	bool acceleration_valid;
	bool direction_valid;
	bool orientation_valid;
};

int app_imu_orientation_init(struct app_imu_orientation *orientation, uint32_t frequency_hz);

int app_imu_orientation_update(struct app_imu_orientation *orientation, const float accel_sensor[3],
			       const float gyro_sensor[3],
			       struct app_imu_orientation_result *result);

#endif
