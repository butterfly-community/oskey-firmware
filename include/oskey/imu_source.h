/* SPDX-License-Identifier: MPL-2.0 */

#ifndef OSKEY_IMU_SOURCE_H_
#define OSKEY_IMU_SOURCE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSKEY_IMU_SOURCE_MAX_BATCH 16U

struct oskey_imu_source_info {
	uint32_t odr_millihz;
	uint32_t poll_interval_us;
	uint16_t gyro_fs_dps;
};

struct oskey_imu_source_sample {
	uint64_t timestamp_ns;
	int32_t accel_ug[3];
	int32_t gyro_10udps[3];
	bool discontinuity;
};

struct oskey_imu_source_batch {
	size_t count;
	struct oskey_imu_source_sample samples[OSKEY_IMU_SOURCE_MAX_BATCH];
};

struct oskey_imu_source_driver_api {
	int (*get_info)(const struct device *dev, struct oskey_imu_source_info *info);
	int (*start)(const struct device *dev);
	int (*stop)(const struct device *dev);
	int (*read_batch)(const struct device *dev, struct oskey_imu_source_batch *batch);
};

static inline int oskey_imu_source_get_info(const struct device *dev,
					    struct oskey_imu_source_info *info)
{
	const struct oskey_imu_source_driver_api *api = dev->api;

	return api->get_info(dev, info);
}

static inline int oskey_imu_source_start(const struct device *dev)
{
	const struct oskey_imu_source_driver_api *api = dev->api;

	return api->start(dev);
}

static inline int oskey_imu_source_stop(const struct device *dev)
{
	const struct oskey_imu_source_driver_api *api = dev->api;

	return api->stop(dev);
}

static inline int oskey_imu_source_read_batch(const struct device *dev,
					      struct oskey_imu_source_batch *batch)
{
	const struct oskey_imu_source_driver_api *api = dev->api;

	return api->read_batch(dev, batch);
}

#ifdef __cplusplus
}
#endif

#endif /* OSKEY_IMU_SOURCE_H_ */
