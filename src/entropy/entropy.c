/* SPDX-License-Identifier: MPL-2.0 */

#define _DEFAULT_SOURCE

#include "entropy.h"

#include <errno.h>
#include <psa/crypto.h>
#include <string.h>
#include <strings.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "app.h"
#include "bus.h"
#if defined(CONFIG_OSKEY_CAMERA)
#include "camera/camera.h"
#endif
#if defined(CONFIG_OSKEY_MICROPHONE)
#include "audio/microphone.h"
#endif

LOG_MODULE_REGISTER(app_entropy);

#define ENTROPY_TRANSCRIPT_VERSION     1U
#define ENTROPY_TRANSCRIPT_HEADER_SIZE 10U
#define ENTROPY_TRANSCRIPT_RECORD_SIZE 41U
#define ENTROPY_AUXILIARY_SOURCE_COUNT 4U
#define ENTROPY_TOUCH_MIN_POINTS       64U
#define ENTROPY_IMU_MIN_SAMPLES        256U
#define ENTROPY_CAMERA_FRAME_COUNT     8U
#define ENTROPY_AUDIO_MIN_FRAMES       48000U
#define ENTROPY_CAMERA_FRAME_WIDTH     320U
#define ENTROPY_CAMERA_FRAME_HEIGHT    240U
#define ENTROPY_CAMERA_THREAD_STACK    4096U

static const uint8_t transcript_magic[4] = {'O', 'S', 'E', 'M'};
static const uint8_t source_domain[] = "OSKEY/ENTROPY-SOURCE/V1";

struct entropy_record {
	uint8_t source;
	uint32_t units;
	uint32_t duration_ms;
	uint8_t digest[32];
};

struct entropy_context {
	psa_hash_operation_t hash;
	struct entropy_record records[ENTROPY_AUXILIARY_SOURCE_COUNT];
	uint32_t session;
	uint32_t units;
	int64_t started_ms;
	uint8_t words;
	uint8_t selected;
	uint8_t completed;
	uint8_t skipped;
	uint8_t current;
	enum app_entropy_state state;
};

BUILD_ASSERT(APP_ENTROPY_TRANSCRIPT_MAX_SIZE ==
	     ENTROPY_TRANSCRIPT_HEADER_SIZE +
		     ENTROPY_AUXILIARY_SOURCE_COUNT * ENTROPY_TRANSCRIPT_RECORD_SIZE);

static struct entropy_context entropy = {
	.hash = PSA_HASH_OPERATION_INIT,
};
K_MUTEX_DEFINE(entropy_lock);

#if defined(CONFIG_OSKEY_CAMERA)
K_SEM_DEFINE(entropy_camera_request, 0, 1);
#endif

static uint8_t entropy_next_source_locked(void)
{
	uint8_t pending = entropy.selected & APP_ENTROPY_SOURCE_AUXILIARY_MASK &
			  ~(entropy.completed | entropy.skipped);

	for (uint8_t source = APP_ENTROPY_SOURCE_TOUCH; source <= APP_ENTROPY_SOURCE_MICROPHONE;
	     source <<= 1) {
		if ((pending & source) != 0U) {
			return source;
		}
	}
	return APP_ENTROPY_SOURCE_NONE;
}

static uint16_t entropy_progress(uint8_t source, uint32_t units)
{
	uint32_t unit_target;

	switch (source) {
	case APP_ENTROPY_SOURCE_TOUCH:
		unit_target = ENTROPY_TOUCH_MIN_POINTS;
		break;
	case APP_ENTROPY_SOURCE_IMU:
		unit_target = ENTROPY_IMU_MIN_SAMPLES;
		break;
	case APP_ENTROPY_SOURCE_CAMERA:
		unit_target = ENTROPY_CAMERA_FRAME_COUNT;
		break;
	case APP_ENTROPY_SOURCE_MICROPHONE:
		unit_target = ENTROPY_AUDIO_MIN_FRAMES;
		break;
	default:
		return 0U;
	}

	return (uint16_t)(MIN(units, unit_target) * 1000U / unit_target);
}

static void entropy_stop_source(uint8_t source)
{
	switch (source) {
#if defined(CONFIG_OSKEY_IMU)
	case APP_ENTROPY_SOURCE_IMU: {
		struct app_imu_command command = {.kind = APP_IMU_COMMAND_STOP};
		(void)zbus_chan_pub(&app_imu_command_chan, &command, K_MSEC(100));
		break;
	}
#endif
#if defined(CONFIG_OSKEY_MICROPHONE)
	case APP_ENTROPY_SOURCE_MICROPHONE:
		app_microphone_entropy_stop();
		break;
#endif
	default:
		break;
	}
}

static int entropy_start_source(uint32_t session, uint8_t source);

static int entropy_advance(uint32_t session)
{
	uint8_t next;

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session != session || entropy.state != APP_ENTROPY_CAPTURING ||
	    entropy.current != APP_ENTROPY_SOURCE_NONE) {
		k_mutex_unlock(&entropy_lock);
		return -ECANCELED;
	}
	next = entropy_next_source_locked();
	if (next == APP_ENTROPY_SOURCE_NONE) {
		entropy.state = APP_ENTROPY_READY;
		entropy.current = APP_ENTROPY_SOURCE_NONE;
		entropy.units = 0U;
		entropy.started_ms = 0;
		k_mutex_unlock(&entropy_lock);
		return 0;
	}
	k_mutex_unlock(&entropy_lock);

	return entropy_start_source(session, next);
}

static bool entropy_set_error(uint32_t session, uint8_t source, int error)
{
	bool matched = false;

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session == session && entropy.state == APP_ENTROPY_CAPTURING &&
	    entropy.current == source) {
		(void)psa_hash_abort(&entropy.hash);
		entropy.hash = psa_hash_operation_init();
		entropy.state = APP_ENTROPY_ERROR;
		matched = true;
		entropy_stop_source(source);
		LOG_WRN("Entropy source %u failed: %d", source, error);
	}
	k_mutex_unlock(&entropy_lock);
	return matched;
}

static int entropy_start_adapter(uint8_t source)
{
	switch (source) {
	case APP_ENTROPY_SOURCE_TOUCH:
		return 0;
#if defined(CONFIG_OSKEY_IMU)
	case APP_ENTROPY_SOURCE_IMU: {
		struct app_imu_command command = {.kind = APP_IMU_COMMAND_START};
		return zbus_chan_pub(&app_imu_command_chan, &command, K_MSEC(100));
	}
#endif
#if defined(CONFIG_OSKEY_CAMERA)
	case APP_ENTROPY_SOURCE_CAMERA:
		k_sem_give(&entropy_camera_request);
		return 0;
#endif
#if defined(CONFIG_OSKEY_MICROPHONE)
	case APP_ENTROPY_SOURCE_MICROPHONE:
		return app_microphone_entropy_start();
#endif
	default:
		return -ENOTSUP;
	}
}

static int entropy_start_source(uint32_t session, uint8_t source)
{
	uint8_t header[2] = {ENTROPY_TRANSCRIPT_VERSION, source};
	int ret;

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session != session || entropy.state != APP_ENTROPY_CAPTURING ||
	    entropy.current != APP_ENTROPY_SOURCE_NONE || (entropy.selected & source) == 0U) {
		k_mutex_unlock(&entropy_lock);
		return -EINVAL;
	}

	entropy.current = source;
	entropy.units = 0U;
	entropy.started_ms = k_uptime_get();
	entropy.hash = psa_hash_operation_init();
	ret = psa_hash_setup(&entropy.hash, PSA_ALG_SHA_256);
	if (ret == PSA_SUCCESS) {
		ret = psa_hash_update(&entropy.hash, source_domain, sizeof(source_domain) - 1U);
	}
	if (ret == PSA_SUCCESS) {
		ret = psa_hash_update(&entropy.hash, header, sizeof(header));
	}
	if (ret == PSA_SUCCESS) {
		ret = entropy_start_adapter(source);
	}
	if (ret < 0) {
		(void)psa_hash_abort(&entropy.hash);
		entropy.hash = psa_hash_operation_init();
		entropy.state = APP_ENTROPY_ERROR;
		LOG_WRN("Unable to initialize entropy source %u: %d", source, ret);
	}
	k_mutex_unlock(&entropy_lock);
	return 0;
}

uint8_t app_entropy_capabilities(void)
{
	uint8_t capabilities = 0U;

	if (app_hardware_rng_available()) {
		capabilities |= APP_ENTROPY_SOURCE_HARDWARE_RNG;
	}

#if defined(CONFIG_OSKEY_DISPLAY) && DT_HAS_CHOSEN(zephyr_touch)
	if (device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)))) {
		capabilities |= APP_ENTROPY_SOURCE_TOUCH;
	}
#endif

#if defined(CONFIG_OSKEY_IMU)
	enum app_imu_state imu_state;
	if (zbus_chan_read(&app_imu_state_chan, &imu_state, K_NO_WAIT) == 0 &&
	    imu_state != APP_IMU_DISABLED && imu_state != APP_IMU_ERROR) {
		capabilities |= APP_ENTROPY_SOURCE_IMU;
	}
#endif

#if defined(CONFIG_OSKEY_CAMERA)
	enum app_camera_state camera_state;
	if (zbus_chan_read(&app_camera_state_chan, &camera_state, K_NO_WAIT) == 0 &&
	    camera_state != APP_CAMERA_DISABLED && camera_state != APP_CAMERA_ERROR) {
		capabilities |= APP_ENTROPY_SOURCE_CAMERA;
	}
#endif

#if defined(CONFIG_OSKEY_MICROPHONE)
	if (app_microphone_ready()) {
		capabilities |= APP_ENTROPY_SOURCE_MICROPHONE;
	}
#endif

	return capabilities;
}

int app_entropy_begin(uint8_t words, uint8_t sources, uint32_t *session)
{
	uint8_t available = app_entropy_capabilities();
	uint8_t selected = sources | APP_ENTROPY_SOURCE_HARDWARE_RNG;

	if (session == NULL || (words != 12U && words != 18U && words != 24U) ||
	    (sources & APP_ENTROPY_SOURCE_AUXILIARY_MASK) == 0U ||
	    (sources & ~APP_ENTROPY_SOURCE_AUXILIARY_MASK) != 0U ||
	    (available & APP_ENTROPY_SOURCE_HARDWARE_RNG) == 0U || (sources & ~available) != 0U) {
		return -EINVAL;
	}
	if (psa_crypto_init() != PSA_SUCCESS) {
		return -EIO;
	}

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.state != APP_ENTROPY_IDLE) {
		k_mutex_unlock(&entropy_lock);
		return -EBUSY;
	}

	uint32_t next_session = entropy.session + 1U;
	if (next_session == 0U) {
		next_session = 1U;
	}
	explicit_bzero(entropy.records, sizeof(entropy.records));
	entropy.session = next_session;
	entropy.words = words;
	entropy.selected = selected;
	entropy.completed = 0U;
	entropy.skipped = 0U;
	entropy.current = APP_ENTROPY_SOURCE_NONE;
	entropy.units = 0U;
	entropy.started_ms = 0;
	entropy.state = APP_ENTROPY_CAPTURING;
	*session = next_session;
	k_mutex_unlock(&entropy_lock);

	return entropy_advance(next_session);
}

static int entropy_complete_source_locked(uint8_t source, uint32_t duration_ms)
{
	if (entropy_progress(source, entropy.units) != 1000U) {
		return 0;
	}

	struct entropy_record *record = &entropy.records[POPCOUNT(entropy.completed)];
	size_t digest_len = 0U;
	psa_status_t status =
		psa_hash_finish(&entropy.hash, record->digest, sizeof(record->digest), &digest_len);

	entropy.hash = psa_hash_operation_init();
	if (status != PSA_SUCCESS || digest_len != sizeof(record->digest)) {
		explicit_bzero(record, sizeof(*record));
		return -EIO;
	}

	record->source = source;
	record->units = entropy.units;
	record->duration_ms = duration_ms;
	entropy.completed |= source;
	entropy.current = APP_ENTROPY_SOURCE_NONE;
	entropy.units = 0U;
	entropy.started_ms = 0;
	entropy_stop_source(source);
	return 1;
}

int app_entropy_feed(uint32_t session, enum app_entropy_source source, const void *data, size_t len,
		     uint32_t units)
{
	uint8_t metadata[8];
	uint32_t duration_ms;
	int ret;

	if (data == NULL || len == 0U || units == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session != session || entropy.state != APP_ENTROPY_CAPTURING ||
	    entropy.current != (uint8_t)source) {
		k_mutex_unlock(&entropy_lock);
		return -ECANCELED;
	}

	duration_ms = (uint32_t)(k_uptime_get() - entropy.started_ms);
	sys_put_le32(duration_ms, &metadata[0]);
	sys_put_le32(units, &metadata[4]);
	psa_status_t status = psa_hash_update(&entropy.hash, metadata, sizeof(metadata));
	if (status == PSA_SUCCESS) {
		status = psa_hash_update(&entropy.hash, data, len);
	}
	if (status != PSA_SUCCESS) {
		k_mutex_unlock(&entropy_lock);
		(void)entropy_set_error(session, (uint8_t)source, -EIO);
		return -EIO;
	}
	entropy.units += units;
	ret = entropy_complete_source_locked((uint8_t)source, duration_ms);
	k_mutex_unlock(&entropy_lock);

	if (ret < 0) {
		(void)entropy_set_error(session, (uint8_t)source, ret);
		return ret;
	}
	if (ret == 1) {
		int advance_ret = entropy_advance(session);

		return advance_ret < 0 ? advance_ret : 1;
	}
	return 0;
}

int app_entropy_fail(uint32_t session, enum app_entropy_source source, int error)
{
	return entropy_set_error(session, (uint8_t)source, error) ? 0 : -ECANCELED;
}

int app_entropy_retry(uint32_t session)
{
	uint8_t source;

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session != session || entropy.state != APP_ENTROPY_ERROR ||
	    entropy.current == APP_ENTROPY_SOURCE_NONE) {
		k_mutex_unlock(&entropy_lock);
		return -EINVAL;
	}
	source = entropy.current;
	entropy.current = APP_ENTROPY_SOURCE_NONE;
	entropy.state = APP_ENTROPY_CAPTURING;
	k_mutex_unlock(&entropy_lock);

	return entropy_start_source(session, source);
}

int app_entropy_skip(uint32_t session)
{
	uint8_t source;

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session != session ||
	    (entropy.state != APP_ENTROPY_CAPTURING && entropy.state != APP_ENTROPY_ERROR) ||
	    entropy.current == APP_ENTROPY_SOURCE_NONE) {
		k_mutex_unlock(&entropy_lock);
		return -EINVAL;
	}
	source = entropy.current;
	(void)psa_hash_abort(&entropy.hash);
	entropy.hash = psa_hash_operation_init();
	entropy.skipped |= source;
	entropy.current = APP_ENTROPY_SOURCE_NONE;
	entropy.state = APP_ENTROPY_CAPTURING;
	entropy_stop_source(source);
	k_mutex_unlock(&entropy_lock);

	return entropy_advance(session);
}

int app_entropy_snapshot_get(struct app_entropy_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&entropy_lock, K_FOREVER);
	*snapshot = (struct app_entropy_snapshot){
		.session = entropy.session,
		.progress_permille = entropy_progress(entropy.current, entropy.units),
		.completed = entropy.completed,
		.skipped = entropy.skipped,
		.current = entropy.current,
		.state = entropy.state,
	};
	k_mutex_unlock(&entropy_lock);
	return 0;
}

int app_entropy_transcript_get(uint32_t session, void *buffer, size_t size, size_t *written)
{
	uint8_t *output = buffer;

	if (buffer == NULL || written == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&entropy_lock, K_FOREVER);
	uint8_t record_count = POPCOUNT(entropy.completed);
	size_t required =
		ENTROPY_TRANSCRIPT_HEADER_SIZE + record_count * ENTROPY_TRANSCRIPT_RECORD_SIZE;
	if (entropy.session != session || entropy.state != APP_ENTROPY_READY) {
		k_mutex_unlock(&entropy_lock);
		return -EAGAIN;
	}
	if (size < required) {
		k_mutex_unlock(&entropy_lock);
		return -ENOSPC;
	}

	memcpy(output, transcript_magic, sizeof(transcript_magic));
	output[4] = ENTROPY_TRANSCRIPT_VERSION;
	output[5] = entropy.words;
	output[6] = entropy.selected;
	output[7] = entropy.completed;
	output[8] = entropy.skipped;
	output[9] = record_count;
	output += ENTROPY_TRANSCRIPT_HEADER_SIZE;

	for (size_t i = 0; i < record_count; i++) {
		const struct entropy_record *record = &entropy.records[i];
		output[0] = record->source;
		sys_put_le32(record->units, &output[1]);
		sys_put_le32(record->duration_ms, &output[5]);
		memcpy(&output[9], record->digest, sizeof(record->digest));
		output += ENTROPY_TRANSCRIPT_RECORD_SIZE;
	}
	*written = required;
	k_mutex_unlock(&entropy_lock);
	return 0;
}

void app_entropy_cancel(uint32_t session)
{
	uint8_t source;

	k_mutex_lock(&entropy_lock, K_FOREVER);
	if (entropy.session != session || entropy.state == APP_ENTROPY_IDLE) {
		k_mutex_unlock(&entropy_lock);
		return;
	}
	source = entropy.current;
	(void)psa_hash_abort(&entropy.hash);
	entropy_stop_source(source);
	uint32_t retained_session = entropy.session;
	explicit_bzero(&entropy, sizeof(entropy));
	entropy.hash = psa_hash_operation_init();
	entropy.session = retained_session;
	entropy.state = APP_ENTROPY_IDLE;
	k_mutex_unlock(&entropy_lock);
}

#if defined(CONFIG_OSKEY_CAMERA)
static void entropy_camera_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	while (true) {
		k_sem_take(&entropy_camera_request, K_FOREVER);
		struct app_entropy_snapshot snapshot;
		if (app_entropy_snapshot_get(&snapshot) < 0 ||
		    snapshot.state != APP_ENTROPY_CAPTURING ||
		    snapshot.current != APP_ENTROPY_SOURCE_CAMERA) {
			continue;
		}

		struct video_format format;
		int ret = app_camera_select_rgb565_format(ENTROPY_CAMERA_FRAME_WIDTH,
							  ENTROPY_CAMERA_FRAME_HEIGHT, &format);
		bool started = false;
		if (ret == 0) {
			ret = app_camera_start(&format);
			started = ret == 0;
		}
		while (ret == 0) {
			struct video_buffer *frame;
			ret = app_camera_frame_get(&frame, K_MSEC(250));
			if (ret == -EAGAIN) {
				ret = 0;
				continue;
			}
			if (ret < 0) {
				break;
			}

			size_t frame_size = MIN(frame->bytesused, (size_t)format.size);
			int feed_ret = app_entropy_feed(snapshot.session, APP_ENTROPY_SOURCE_CAMERA,
							frame->buffer, frame_size, 1U);
			int release_ret = app_camera_frame_release(frame);
			if (release_ret < 0) {
				ret = release_ret;
				break;
			}
			if (feed_ret == 1 || feed_ret == -ECANCELED) {
				break;
			}
			if (feed_ret < 0) {
				ret = feed_ret;
				break;
			}
			k_sleep(K_MSEC(200));
		}

		if (started) {
			int stop_ret = app_camera_stop();
			if (ret == 0 && stop_ret < 0) {
				ret = stop_ret;
			}
		}
		if (ret < 0 && ret != -ECANCELED) {
			(void)app_entropy_fail(snapshot.session, APP_ENTROPY_SOURCE_CAMERA, ret);
		}
	}
}

K_THREAD_DEFINE(entropy_camera_thread_id, ENTROPY_CAMERA_THREAD_STACK, entropy_camera_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);
#endif
