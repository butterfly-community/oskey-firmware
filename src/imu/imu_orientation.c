/* SPDX-License-Identifier: Apache-2.0 */

#include "imu_orientation.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include <zsl/vectors.h>

#define IMU_STANDARD_GRAVITY        9.80665f
#define IMU_INITIAL_ACCEL_ERROR     (0.20f * IMU_STANDARD_GRAVITY)
#define IMU_MIN_QUATERNION_NORM     0.000001f
#define IMU_AQUA_ACCELEROMETER_GAIN 0.02f

static void imu_sensor_to_fusion(const float sensor[3], float fusion[3])
{
	fusion[0] = sensor[1];
	fusion[1] = -sensor[0];
	fusion[2] = sensor[2];
}

static void imu_orientation_reset(struct app_imu_orientation *orientation)
{
	orientation->quaternion = (struct zsl_quat){.r = 1.0f};
	orientation->initialized = false;
}

int app_imu_orientation_init(struct app_imu_orientation *orientation, uint32_t frequency_hz)
{
	if (orientation == NULL || frequency_hz == 0U) {
		return -EINVAL;
	}

	memset(orientation, 0, sizeof(*orientation));
	orientation->config = (struct zsl_fus_aqua_cfg){
		.alpha = IMU_AQUA_ACCELEROMETER_GAIN,
		.beta = 0.0f,
		.e_a = 0.9f,
		.e_m = 0.9f,
	};
	imu_orientation_reset(orientation);
	return zsl_fus_aqua_init(frequency_hz, &orientation->config);
}

int app_imu_orientation_update(struct app_imu_orientation *orientation, const float accel_sensor[3],
			       const float gyro_sensor[3],
			       struct app_imu_orientation_result *result)
{
	if (orientation == NULL || accel_sensor == NULL || gyro_sensor == NULL || result == NULL) {
		return -EINVAL;
	}

	memset(result, 0, sizeof(*result));

	float accel[3];
	float gyro[3];

	imu_sensor_to_fusion(accel_sensor, accel);
	imu_sensor_to_fusion(gyro_sensor, gyro);

	float accel_norm = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);

	if (!orientation->initialized &&
	    (!isfinite(accel_norm) ||
	     fabsf(accel_norm - IMU_STANDARD_GRAVITY) > IMU_INITIAL_ACCEL_ERROR)) {
		return 0;
	}

	ZSL_VECTOR_DEF(accel_vector, 3);
	ZSL_VECTOR_DEF(gyro_vector, 3);

	for (size_t i = 0; i < 3; i++) {
		accel_vector.data[i] = accel[i];
		gyro_vector.data[i] = gyro[i];
	}

	int ret = zsl_fus_aqua_feed(&accel_vector, NULL, &gyro_vector, NULL,
				    &orientation->quaternion, &orientation->config);

	if (ret < 0) {
		return ret;
	}

	float qw = orientation->quaternion.r;
	float qx = -orientation->quaternion.i;
	float qy = orientation->quaternion.j;
	float qz = orientation->quaternion.k;
	float norm = sqrtf(qw * qw + qx * qx + qy * qy + qz * qz);

	if (!isfinite(norm) || norm < IMU_MIN_QUATERNION_NORM) {
		imu_orientation_reset(orientation);
		return 0;
	}

	result->quaternion_w = qw / norm;
	result->quaternion_x = qx / norm;
	result->quaternion_y = qy / norm;
	result->quaternion_z = qz / norm;
	result->valid = true;
	orientation->initialized = true;
	return 0;
}
