/* SPDX-License-Identifier: Apache-2.0 */

#include "imu_orientation.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include <zsl/vectors.h>

#define IMU_PI                       3.14159265358979323846f
#define IMU_RAD_TO_DEG               (180.0f / IMU_PI)
#define IMU_STANDARD_GRAVITY         9.80665f
#define IMU_ACCEL_NORM_MIN           (0.60f * IMU_STANDARD_GRAVITY)
#define IMU_ACCEL_NORM_MAX           (1.40f * IMU_STANDARD_GRAVITY)
#define IMU_INITIAL_ACCEL_NORM_MIN   (0.85f * IMU_STANDARD_GRAVITY)
#define IMU_INITIAL_ACCEL_NORM_MAX   (1.15f * IMU_STANDARD_GRAVITY)
#define IMU_DIRECTION_MIN_HORIZONTAL 0.05f
#define IMU_AQUA_ACCEL_GAIN          0.02f
#define IMU_GYRO_BIAS_MAX_RATE       (2.0f / IMU_RAD_TO_DEG)
#define IMU_GYRO_BIAS_SAMPLES        32U
#define IMU_MIN_NORM                 0.000001f

static float imu_clamp(float value, float lower, float upper)
{
	return fmaxf(lower, fminf(upper, value));
}

static void imu_sensor_to_fusion(const float sensor[3], float fusion[3])
{
	fusion[0] = sensor[1];
	fusion[1] = -sensor[0];
	fusion[2] = sensor[2];
}

static void imu_orientation_from_accel(struct app_imu_orientation *orientation,
				       const float accel[3], float norm)
{
	float x = accel[0] / norm;
	float y = accel[1] / norm;
	float z = imu_clamp(accel[2] / norm, -1.0f, 1.0f);

	if (z < -0.9999f) {
		orientation->quaternion = (struct zsl_quat){
			.r = 0.0f,
			.i = 1.0f,
			.j = 0.0f,
			.k = 0.0f,
		};
		return;
	}

	float denominator = sqrtf(2.0f * (1.0f + z));

	orientation->quaternion.r = denominator * 0.5f;
	orientation->quaternion.i = -y / denominator;
	orientation->quaternion.j = x / denominator;
	orientation->quaternion.k = 0.0f;
}

static void imu_update_gyro_bias(struct app_imu_orientation *orientation, const float gyro[3],
				 float accel_norm)
{
	if (orientation->gyro_bias_ready || accel_norm < IMU_INITIAL_ACCEL_NORM_MIN ||
	    accel_norm > IMU_INITIAL_ACCEL_NORM_MAX) {
		return;
	}

	float gyro_norm = sqrtf(gyro[0] * gyro[0] + gyro[1] * gyro[1] + gyro[2] * gyro[2]);

	if (gyro_norm > IMU_GYRO_BIAS_MAX_RATE) {
		return;
	}

	for (size_t i = 0; i < 3; i++) {
		orientation->gyro_bias_sum[i] += gyro[i];
	}
	orientation->gyro_bias_samples++;

	if (orientation->gyro_bias_samples == IMU_GYRO_BIAS_SAMPLES) {
		for (size_t i = 0; i < 3; i++) {
			orientation->gyro_bias[i] =
				orientation->gyro_bias_sum[i] / IMU_GYRO_BIAS_SAMPLES;
		}
		orientation->gyro_bias_ready = true;
	}
}

static bool imu_accel_correction_safe(const struct app_imu_orientation *orientation,
				      const float accel[3], float accel_norm)
{
	const struct zsl_quat *q = &orientation->quaternion;
	float x = accel[0] / accel_norm;
	float y = accel[1] / accel_norm;
	float z = accel[2] / accel_norm;
	float rotated_z = 2.0f * (q->i * q->k + q->r * q->j) * x +
			  2.0f * (q->j * q->k - q->r * q->i) * y +
			  (1.0f - 2.0f * (q->i * q->i + q->j * q->j)) * z;

	return rotated_z > -0.999f;
}

static int imu_orientation_result(struct app_imu_orientation *orientation,
				  struct app_imu_orientation_result *result)
{
	struct zsl_quat *q = &orientation->quaternion;
	float qw = q->r;
	float qx = -q->i;
	float qy = q->j;
	float qz = q->k;
	float norm = sqrtf(qw * qw + qx * qx + qy * qy + qz * qz);

	if (!isfinite(norm) || norm < IMU_MIN_NORM) {
		return -ERANGE;
	}

	qw /= norm;
	qx /= norm;
	qy /= norm;
	qz /= norm;

	float matrix_20 = 2.0f * (qx * qz - qw * qy);
	float matrix_21 = 2.0f * (qy * qz + qw * qx);
	float matrix_22 = 1.0f - 2.0f * (qx * qx + qy * qy);
	float horizontal = sqrtf(matrix_20 * matrix_20 + matrix_21 * matrix_21);

	result->tilt = acosf(imu_clamp(matrix_22, -1.0f, 1.0f)) * IMU_RAD_TO_DEG;
	result->direction = atan2f(-matrix_20, matrix_21) * IMU_RAD_TO_DEG;
	result->direction_valid = horizontal >= IMU_DIRECTION_MIN_HORIZONTAL;
	result->orientation_valid = true;
	result->quaternion_w = qw;
	result->quaternion_x = qx;
	result->quaternion_y = qy;
	result->quaternion_z = qz;
	return 0;
}

int app_imu_orientation_init(struct app_imu_orientation *orientation, uint32_t frequency_hz)
{
	if (orientation == NULL || frequency_hz == 0U) {
		return -EINVAL;
	}

	memset(orientation, 0, sizeof(*orientation));
	orientation->config = (struct zsl_fus_aqua_cfg){
		.alpha = IMU_AQUA_ACCEL_GAIN,
		.beta = 0.0f,
		.e_a = 0.9f,
		.e_m = 0.9f,
	};
	orientation->quaternion.r = 1.0f;
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
	bool acceleration_valid =
		accel_norm >= IMU_ACCEL_NORM_MIN && accel_norm <= IMU_ACCEL_NORM_MAX;

	result->acceleration_valid = acceleration_valid;
	imu_update_gyro_bias(orientation, gyro, accel_norm);

	if (!orientation->initialized) {
		if (accel_norm < IMU_INITIAL_ACCEL_NORM_MIN ||
		    accel_norm > IMU_INITIAL_ACCEL_NORM_MAX) {
			return 0;
		}
		imu_orientation_from_accel(orientation, accel, accel_norm);
		orientation->initialized = true;
	}

	ZSL_VECTOR_DEF(accel_vector, 3);
	ZSL_VECTOR_DEF(gyro_vector, 3);

	for (size_t i = 0; i < 3; i++) {
		accel_vector.data[i] = accel[i];
		gyro_vector.data[i] = gyro[i] - orientation->gyro_bias[i];
	}

	bool correction_safe =
		acceleration_valid && imu_accel_correction_safe(orientation, accel, accel_norm);
	int ret = zsl_fus_aqua_feed(correction_safe ? &accel_vector : NULL, NULL, &gyro_vector,
				    NULL, &orientation->quaternion, &orientation->config);

	if (ret < 0) {
		return ret;
	}

	return imu_orientation_result(orientation, result);
}
