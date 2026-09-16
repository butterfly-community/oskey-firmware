/* SPDX-License-Identifier: MPL-2.0 */

#include "imu.h"

#include <Fusion.h>
#include <errno.h>
#include <oskey/imu_source.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "bus.h"
#if defined(CONFIG_OSKEY_DISPLAY) && defined(CONFIG_OSKEY_RUST)
#include "entropy/entropy.h"
#endif

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_ERROR_THRESHOLD   5
#define IMU_RECOVERY_DELAY_MS 100
#define IMU_STATS_INTERVAL_MS 5000
#define IMU_MICRO_G_PER_G     1000000.0f
#define IMU_10UDEG_PER_DEGREE 100000.0f

static const struct device *const imu_source = DEVICE_DT_GET(IMU_NODE);
static struct oskey_imu_source_info imu_source_info;
static enum app_imu_state imu_state = APP_IMU_DISABLED;

struct imu_runtime {
	FusionAhrs ahrs;
	FusionBias bias;
	uint64_t last_timestamp_ns;
	uint8_t consecutive_errors;
	bool running;
#if defined(CONFIG_OSKEY_LOG)
	int64_t stats_started_ms;
	uint32_t stats_batches;
	uint32_t stats_samples;
	uint32_t stats_errors;
	uint32_t stats_discontinuities;
	uint32_t stats_max_age_us;
	uint8_t stats_max_batch;
#endif
};

static atomic_t imu_clients;
BUILD_ASSERT(APP_IMU_CLIENT_COUNT <= ATOMIC_BITS);
K_SEM_DEFINE(imu_wakeup, 0, 1);

static void imu_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_sem_give(&imu_wakeup);
}

K_TIMER_DEFINE(imu_timer, imu_timer_expiry, NULL);

static void imu_publish_state(enum app_imu_state state)
{
	if (state == imu_state) {
		return;
	}

	const int ret = zbus_chan_pub(&app_imu_state_chan, &state, K_MSEC(10));

	if (ret < 0) {
		LOG_WRN("IMU state publication failed: %d", ret);
		return;
	}
	imu_state = state;
}

#if defined(CONFIG_OSKEY_LOG)
static void imu_stats_reset(struct imu_runtime *runtime, int64_t now_ms)
{
	runtime->stats_started_ms = now_ms;
	runtime->stats_batches = 0U;
	runtime->stats_samples = 0U;
	runtime->stats_errors = 0U;
	runtime->stats_discontinuities = 0U;
	runtime->stats_max_age_us = 0U;
	runtime->stats_max_batch = 0U;
}

static void imu_stats_record_batch(struct imu_runtime *runtime, int status, size_t count)
{
	const int64_t now_ms = k_uptime_get();

	if (runtime->stats_started_ms == 0) {
		imu_stats_reset(runtime, now_ms);
	}

	runtime->stats_batches += (status == 0) && (count > 0U);
	runtime->stats_samples += count;
	runtime->stats_errors += status < 0;
	runtime->stats_max_batch = MAX(runtime->stats_max_batch, (uint8_t)count);

	if ((now_ms - runtime->stats_started_ms) >= IMU_STATS_INTERVAL_MS) {
		LOG_INF("IMU acquisition: samples=%u batches=%u errors=%u gaps=%u max_batch=%u "
			"max_age=%u us",
			runtime->stats_samples, runtime->stats_batches, runtime->stats_errors,
			runtime->stats_discontinuities, runtime->stats_max_batch,
			runtime->stats_max_age_us);
		imu_stats_reset(runtime, now_ms);
	}
}

static void imu_stats_record_sample(struct imu_runtime *runtime,
				    const struct oskey_imu_source_sample *sample,
				    uint64_t arrival_timestamp_ns)
{
	const uint64_t age_ns = arrival_timestamp_ns >= sample->timestamp_ns
					? arrival_timestamp_ns - sample->timestamp_ns
					: 0U;

	runtime->stats_discontinuities += sample->discontinuity;
	runtime->stats_max_age_us =
		MAX(runtime->stats_max_age_us, (uint32_t)MIN(age_ns / 1000U, (uint64_t)UINT32_MAX));
}
#endif

static float imu_sample_period(void)
{
	return 1000.0f / (float)imu_source_info.odr_millihz;
}

static void imu_fusion_initialize(struct imu_runtime *runtime)
{
	const float sample_rate = (float)imu_source_info.odr_millihz / 1000.0f;
	FusionAhrsSettings ahrs_settings = fusionAhrsDefaultSettings;
	FusionBiasSettings bias_settings = fusionBiasDefaultSettings;

	FusionAhrsInitialise(&runtime->ahrs);
	ahrs_settings.sampleRate = sample_rate;
	ahrs_settings.gyroscopeRange = imu_source_info.gyro_fs_dps;
	ahrs_settings.accelerationRejection = 10.0f;
	ahrs_settings.magneticRejection = 0.0f;
	ahrs_settings.rejectionTimeout = 5.0f;
	FusionAhrsSetSettings(&runtime->ahrs, &ahrs_settings);

	FusionBiasInitialise(&runtime->bias);
	bias_settings.sampleRate = sample_rate;
	FusionBiasSetSettings(&runtime->bias, &bias_settings);
	FusionAhrsSetSamplePeriod(&runtime->ahrs, imu_sample_period());
}

static void imu_fusion_restart(struct imu_runtime *runtime)
{
	FusionAhrsRestart(&runtime->ahrs);
	FusionAhrsSetSamplePeriod(&runtime->ahrs, imu_sample_period());
}

static FusionVector imu_accel_to_fusion(const struct oskey_imu_source_sample *sample)
{
	return (FusionVector){.axis = {
				      .x = (float)sample->accel_ug[1] / IMU_MICRO_G_PER_G,
				      .y = -(float)sample->accel_ug[0] / IMU_MICRO_G_PER_G,
				      .z = (float)sample->accel_ug[2] / IMU_MICRO_G_PER_G,
			      }};
}

static FusionVector imu_gyro_to_fusion(const struct oskey_imu_source_sample *sample)
{
	return (FusionVector){.axis = {
				      .x = (float)sample->gyro_10udps[1] / IMU_10UDEG_PER_DEGREE,
				      .y = -(float)sample->gyro_10udps[0] / IMU_10UDEG_PER_DEGREE,
				      .z = (float)sample->gyro_10udps[2] / IMU_10UDEG_PER_DEGREE,
			      }};
}

static void imu_quaternion_to_render_matrix(FusionQuaternion quaternion, float rotation[9])
{
	quaternion.element.y = -quaternion.element.y;
	quaternion.element.z = -quaternion.element.z;
	const FusionMatrix matrix = FusionQuaternionToMatrix(quaternion);

	memcpy(rotation, matrix.array, sizeof(matrix.array));
}

static void imu_feed_entropy(const struct oskey_imu_source_batch *batch)
{
#if defined(CONFIG_OSKEY_DISPLAY) && defined(CONFIG_OSKEY_RUST)
	struct app_entropy_snapshot entropy_snapshot;

	if ((batch->count > 0U) && app_entropy_snapshot_get(&entropy_snapshot) == 0 &&
	    entropy_snapshot.state == APP_ENTROPY_CAPTURING &&
	    entropy_snapshot.current == APP_ENTROPY_SOURCE_IMU) {
		uint8_t raw[OSKEY_IMU_SOURCE_MAX_BATCH * 6U * sizeof(uint32_t)];

		for (size_t i = 0U; i < batch->count; i++) {
			const struct oskey_imu_source_sample *sample = &batch->samples[i];
			uint8_t *encoded = &raw[i * 6U * sizeof(uint32_t)];

			for (size_t axis = 0U; axis < 3U; axis++) {
				sys_put_le32((uint32_t)sample->accel_ug[axis],
					     &encoded[axis * sizeof(uint32_t)]);
				sys_put_le32((uint32_t)sample->gyro_10udps[axis],
					     &encoded[(axis + 3U) * sizeof(uint32_t)]);
			}
		}
		(void)app_entropy_feed(entropy_snapshot.session, APP_ENTROPY_SOURCE_IMU, raw,
				       batch->count * 6U * sizeof(uint32_t), batch->count);
	}
#else
	ARG_UNUSED(batch);
#endif
}

static void imu_process_sample(struct imu_runtime *runtime,
			       const struct oskey_imu_source_sample *raw)
{
	if (raw->discontinuity) {
		imu_fusion_restart(runtime);
	}

	const FusionVector accel = imu_accel_to_fusion(raw);
	FusionVector gyro = FusionBiasUpdate(&runtime->bias, imu_gyro_to_fusion(raw));

	FusionAhrsUpdateNoMagnetometer(&runtime->ahrs, gyro, accel);

	struct app_imu_sample sample = {
		.valid = !FusionAhrsGetFlags(&runtime->ahrs).startup,
	};

	imu_quaternion_to_render_matrix(FusionAhrsGetQuaternion(&runtime->ahrs), sample.rotation);
	(void)zbus_chan_pub(&app_imu_sample_chan, &sample, K_NO_WAIT);
	imu_publish_state(sample.valid ? APP_IMU_READY : APP_IMU_INITIALIZING);
}

static int imu_process_batch(struct imu_runtime *runtime,
			     const struct oskey_imu_source_batch *batch)
{
	uint64_t last_timestamp_ns = runtime->last_timestamp_ns;

	if (batch->count > OSKEY_IMU_SOURCE_MAX_BATCH) {
		return -EOVERFLOW;
	}
	for (size_t i = 0U; i < batch->count; i++) {
		const struct oskey_imu_source_sample *sample = &batch->samples[i];

		if ((sample->timestamp_ns == 0U) ||
		    ((last_timestamp_ns != 0U) && !sample->discontinuity &&
		     (sample->timestamp_ns <= last_timestamp_ns))) {
			return -EBADMSG;
		}
		last_timestamp_ns = sample->timestamp_ns;
	}

#if defined(CONFIG_OSKEY_LOG)
	const uint64_t arrival_timestamp_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
#endif
	for (size_t i = 0U; i < batch->count; i++) {
#if defined(CONFIG_OSKEY_LOG)
		imu_stats_record_sample(runtime, &batch->samples[i], arrival_timestamp_ns);
#endif
		imu_process_sample(runtime, &batch->samples[i]);
	}
	imu_feed_entropy(batch);
	runtime->last_timestamp_ns = last_timestamp_ns;
	return 0;
}

static void imu_report_error(int error)
{
	imu_publish_state(APP_IMU_ERROR);
	LOG_ERR("IMU acquisition failed: %d", error);

#if defined(CONFIG_OSKEY_DISPLAY) && defined(CONFIG_OSKEY_RUST)
	struct app_entropy_snapshot entropy_snapshot;

	if (app_entropy_snapshot_get(&entropy_snapshot) == 0 &&
	    entropy_snapshot.state == APP_ENTROPY_CAPTURING &&
	    entropy_snapshot.current == APP_ENTROPY_SOURCE_IMU) {
		(void)app_entropy_fail(entropy_snapshot.session, APP_ENTROPY_SOURCE_IMU, error);
	}
#else
	ARG_UNUSED(error);
#endif
}

static int imu_start_source(struct imu_runtime *runtime)
{
	int ret = oskey_imu_source_start(imu_source);

	if (ret < 0) {
		return ret;
	}

	runtime->running = true;
	runtime->last_timestamp_ns = 0U;
	runtime->consecutive_errors = 0U;
	imu_fusion_restart(runtime);
	k_timer_start(&imu_timer, K_USEC(imu_source_info.poll_interval_us),
		      K_USEC(imu_source_info.poll_interval_us));
#if defined(CONFIG_OSKEY_LOG)
	imu_stats_reset(runtime, k_uptime_get());
	LOG_INF("IMU acquisition started");
#endif
	imu_publish_state(APP_IMU_INITIALIZING);
	return 0;
}

static void imu_stop_source(struct imu_runtime *runtime)
{
	if (!runtime->running) {
		return;
	}

	k_timer_stop(&imu_timer);
	const int ret = oskey_imu_source_stop(imu_source);

	if (ret < 0) {
		LOG_ERR("IMU source stop failed: %d", ret);
	} else {
		LOG_INF("IMU acquisition stopped");
	}
	runtime->running = false;
}

static void imu_handle_failure(struct imu_runtime *runtime, int error)
{
#if defined(CONFIG_OSKEY_LOG)
	imu_stats_record_batch(runtime, error, 0U);
#endif
	if (runtime->consecutive_errors < IMU_ERROR_THRESHOLD) {
		LOG_WRN("IMU acquisition event failed: %d", error);
		runtime->consecutive_errors++;
	}
	if (runtime->consecutive_errors < IMU_ERROR_THRESHOLD) {
		return;
	}

	if (runtime->consecutive_errors == IMU_ERROR_THRESHOLD) {
		imu_report_error(error);
		runtime->consecutive_errors++;
	}
	imu_stop_source(runtime);
}

static void imu_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	struct imu_runtime runtime = {0};

	imu_fusion_initialize(&runtime);
	imu_publish_state(APP_IMU_IDLE);

	while (true) {
		if (atomic_get(&imu_clients) == 0) {
			imu_stop_source(&runtime);
			imu_publish_state(APP_IMU_IDLE);
			k_sem_take(&imu_wakeup, K_FOREVER);
			continue;
		}

		if (!runtime.running) {
			const int ret = imu_start_source(&runtime);

			if (ret < 0) {
				imu_handle_failure(&runtime, ret);
				k_sem_take(&imu_wakeup, K_MSEC(IMU_RECOVERY_DELAY_MS));
				continue;
			}
			continue;
		}

		k_sem_take(&imu_wakeup, K_FOREVER);
		if (atomic_get(&imu_clients) == 0) {
			continue;
		}

		struct oskey_imu_source_batch batch = {0};
		int ret = oskey_imu_source_read_batch(imu_source, &batch);

		if (ret == 0) {
			ret = imu_process_batch(&runtime, &batch);
		}
#if defined(CONFIG_OSKEY_LOG)
		if (ret == 0) {
			imu_stats_record_batch(&runtime, 0, batch.count);
		}
#endif
		if (ret < 0) {
			imu_handle_failure(&runtime, ret);
			if (!runtime.running) {
				k_sem_take(&imu_wakeup, K_MSEC(IMU_RECOVERY_DELAY_MS));
			}
			continue;
		}

		runtime.consecutive_errors = 0U;
	}
}

K_THREAD_DEFINE(imu_thread_id, CONFIG_OSKEY_IMU_THREAD_STACK_SIZE, imu_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(CONFIG_OSKEY_IMU_THREAD_PRIORITY), 0, SYS_FOREVER_MS);

static void imu_command_listener(const struct zbus_channel *chan)
{
	const struct app_imu_command *command = zbus_chan_const_msg(chan);

	if ((unsigned int)command->client >= APP_IMU_CLIENT_COUNT) {
		return;
	}

	if (command->kind == APP_IMU_COMMAND_START) {
		atomic_set_bit(&imu_clients, command->client);
	} else if (command->kind == APP_IMU_COMMAND_STOP) {
		atomic_clear_bit(&imu_clients, command->client);
	} else {
		return;
	}
	k_sem_give(&imu_wakeup);
}

ZBUS_LISTENER_DEFINE(imu_command_listener_ob, imu_command_listener);
ZBUS_CHAN_ADD_OBS(app_imu_command_chan, imu_command_listener_ob, 0);

int app_imu_init(void)
{
	if (!device_is_ready(imu_source)) {
		LOG_ERR("IMU source is not ready");
		imu_publish_state(APP_IMU_ERROR);
		return -ENODEV;
	}

	const int ret = oskey_imu_source_get_info(imu_source, &imu_source_info);

	if ((ret < 0) || (imu_source_info.odr_millihz == 0U) ||
	    (imu_source_info.poll_interval_us == 0U) || (imu_source_info.gyro_fs_dps == 0U)) {
		LOG_ERR("Invalid IMU source: %d", ret);
		imu_publish_state(APP_IMU_ERROR);
		return ret < 0 ? ret : -EINVAL;
	}

	k_thread_start(imu_thread_id);
	return 0;
}
