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
#include <zephyr/smf.h>
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

#define ENTROPY_TRANSCRIPT_VERSION        1U
#define ENTROPY_TRANSCRIPT_HEADER_SIZE    10U
#define ENTROPY_TRANSCRIPT_RECORD_SIZE    41U
#define ENTROPY_AUXILIARY_SOURCE_COUNT    4U
#define ENTROPY_TOUCH_MIN_POINTS          64U
#define ENTROPY_IMU_MIN_SAMPLES           256U
#define ENTROPY_CAMERA_FRAME_COUNT        8U
#define ENTROPY_AUDIO_MIN_FRAMES          48000U
#define ENTROPY_CAMERA_FRAME_WIDTH        320U
#define ENTROPY_CAMERA_FRAME_HEIGHT       240U
#define ENTROPY_CAMERA_SAMPLE_INTERVAL_MS 200U
#define ENTROPY_CAMERA_THREAD_STACK       4096U

static const uint8_t transcript_magic[4] = {'O', 'S', 'E', 'M'};
static const uint8_t source_domain[] = "OSKEY/ENTROPY-SOURCE/V1";

struct entropy_record {
	uint8_t source;
	uint32_t units;
	uint32_t duration_ms;
	uint8_t digest[32];
};

enum entropy_event_kind {
	ENTROPY_EVENT_NONE,
	ENTROPY_EVENT_BEGIN,
	ENTROPY_EVENT_FEED,
	ENTROPY_EVENT_FAIL,
	ENTROPY_EVENT_RETRY,
	ENTROPY_EVENT_SKIP,
	ENTROPY_EVENT_CANCEL,
};

enum entropy_smf_state {
	ENTROPY_SMF_IDLE,
	ENTROPY_SMF_ACTIVE,
	ENTROPY_SMF_TOUCH,
	ENTROPY_SMF_IMU,
	ENTROPY_SMF_CAMERA,
	ENTROPY_SMF_MICROPHONE,
	ENTROPY_SMF_SOURCE_ERROR,
	ENTROPY_SMF_READY,
};

struct entropy_event {
	const void *data;
	size_t len;
	uint32_t session;
	uint32_t units;
	int error;
	uint8_t source;
	enum entropy_event_kind kind;
};

struct entropy_context {
	/* The SMF context must be the first member. */
	struct smf_ctx ctx;
	psa_hash_operation_t hash;
	struct entropy_record records[ENTROPY_AUXILIARY_SOURCE_COUNT];
	struct entropy_event event;
	uint32_t session;
	uint32_t units;
	int64_t started_ms;
	int event_result;
	int last_error;
	uint8_t words;
	uint8_t selected;
	uint8_t completed;
	uint8_t skipped;
	uint8_t current;
	bool hash_active;
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

static const struct smf_state entropy_states[];

static int entropy_imu_start(void)
{
#if defined(CONFIG_OSKEY_IMU)
	struct app_imu_command command = {.kind = APP_IMU_COMMAND_START};

	return zbus_chan_pub(&app_imu_command_chan, &command, K_MSEC(100));
#else
	return -ENOTSUP;
#endif
}

static void entropy_imu_stop(void)
{
#if defined(CONFIG_OSKEY_IMU)
	struct app_imu_command command = {.kind = APP_IMU_COMMAND_STOP};

	(void)zbus_chan_pub(&app_imu_command_chan, &command, K_MSEC(100));
#endif
}

static int entropy_camera_start(void)
{
#if defined(CONFIG_OSKEY_CAMERA)
	k_sem_give(&entropy_camera_request);
	return 0;
#else
	return -ENOTSUP;
#endif
}

static int entropy_microphone_start(void)
{
#if defined(CONFIG_OSKEY_MICROPHONE)
	return app_microphone_entropy_start();
#else
	return -ENOTSUP;
#endif
}

static void entropy_microphone_stop(void)
{
#if defined(CONFIG_OSKEY_MICROPHONE)
	app_microphone_entropy_stop();
#endif
}

struct entropy_source_ops {
	const struct smf_state *state;
	int (*start)(void);
	void (*stop)(void);
	uint32_t target_units;
	uint8_t source;
};

static const struct entropy_source_ops entropy_sources[] = {
	{&entropy_states[ENTROPY_SMF_TOUCH], NULL, NULL, ENTROPY_TOUCH_MIN_POINTS,
	 APP_ENTROPY_SOURCE_TOUCH},
	{&entropy_states[ENTROPY_SMF_IMU], entropy_imu_start, entropy_imu_stop,
	 ENTROPY_IMU_MIN_SAMPLES, APP_ENTROPY_SOURCE_IMU},
	{&entropy_states[ENTROPY_SMF_CAMERA], entropy_camera_start, NULL,
	 ENTROPY_CAMERA_FRAME_COUNT, APP_ENTROPY_SOURCE_CAMERA},
	{&entropy_states[ENTROPY_SMF_MICROPHONE], entropy_microphone_start, entropy_microphone_stop,
	 ENTROPY_AUDIO_MIN_FRAMES, APP_ENTROPY_SOURCE_MICROPHONE},
};

static const struct entropy_source_ops *entropy_source_get(uint8_t source)
{
	for (size_t i = 0; i < ARRAY_SIZE(entropy_sources); i++) {
		if (entropy_sources[i].source == source) {
			return &entropy_sources[i];
		}
	}
	return NULL;
}

static const struct entropy_source_ops *entropy_source_from_state(const struct smf_state *state)
{
	for (size_t i = 0; i < ARRAY_SIZE(entropy_sources); i++) {
		if (entropy_sources[i].state == state) {
			return &entropy_sources[i];
		}
	}
	return NULL;
}

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
	const struct entropy_source_ops *ops = entropy_source_get(source);

	if (ops == NULL) {
		return 0U;
	}
	return (uint16_t)(MIN(units, ops->target_units) * 1000U / ops->target_units);
}

static enum app_entropy_state entropy_state_locked(void)
{
	const struct smf_state *state = smf_get_current_leaf_state(SMF_CTX(&entropy));

	if (state == NULL || state == &entropy_states[ENTROPY_SMF_IDLE]) {
		return APP_ENTROPY_IDLE;
	}
	if (state == &entropy_states[ENTROPY_SMF_READY]) {
		return APP_ENTROPY_READY;
	}
	return state == &entropy_states[ENTROPY_SMF_SOURCE_ERROR] ? APP_ENTROPY_ERROR
								  : APP_ENTROPY_CAPTURING;
}

static void entropy_reset_session_locked(void)
{
	if (entropy.hash_active) {
		(void)psa_hash_abort(&entropy.hash);
	}
	entropy.hash = psa_hash_operation_init();
	entropy.hash_active = false;
	explicit_bzero(entropy.records, sizeof(entropy.records));
	entropy.units = 0U;
	entropy.started_ms = 0;
	entropy.last_error = 0;
	entropy.words = 0U;
	entropy.selected = 0U;
	entropy.completed = 0U;
	entropy.skipped = 0U;
	entropy.current = APP_ENTROPY_SOURCE_NONE;
}

static void entropy_idle_entry(void *object)
{
	ARG_UNUSED(object);
	entropy_reset_session_locked();
}

static void entropy_ready_entry(void *object)
{
	ARG_UNUSED(object);
	entropy.current = APP_ENTROPY_SOURCE_NONE;
	entropy.units = 0U;
	entropy.started_ms = 0;
}

static void entropy_source_exit(void *object)
{
	struct entropy_context *context = object;
	const struct entropy_source_ops *ops =
		entropy_source_from_state(smf_get_current_executing_state(SMF_CTX(context)));

	if (ops != NULL && ops->stop != NULL) {
		ops->stop();
	}
	if (context->hash_active) {
		(void)psa_hash_abort(&context->hash);
	}
	context->hash = psa_hash_operation_init();
	context->hash_active = false;
	context->units = 0U;
	context->started_ms = 0;
}

static void entropy_error_entry(void *object)
{
	struct entropy_context *context = object;

	LOG_WRN("Entropy source %u failed: %d", context->current, context->last_error);
}

static void entropy_source_entry(void *object)
{
	struct entropy_context *context = object;
	const struct entropy_source_ops *ops =
		entropy_source_from_state(smf_get_current_executing_state(SMF_CTX(context)));

	if (ops == NULL) {
		context->last_error = -EINVAL;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_SOURCE_ERROR]);
		return;
	}

	uint8_t header[2] = {ENTROPY_TRANSCRIPT_VERSION, ops->source};
	context->current = ops->source;
	context->units = 0U;
	context->started_ms = k_uptime_get();
	context->hash = psa_hash_operation_init();
	int ret = psa_hash_setup(&context->hash, PSA_ALG_SHA_256);

	if (ret == PSA_SUCCESS) {
		context->hash_active = true;
		ret = psa_hash_update(&context->hash, source_domain, sizeof(source_domain) - 1U);
	}
	if (ret == PSA_SUCCESS) {
		ret = psa_hash_update(&context->hash, header, sizeof(header));
	}
	if (ret == PSA_SUCCESS && ops->start != NULL) {
		ret = ops->start();
	}
	if (ret != PSA_SUCCESS) {
		context->last_error = ret;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_SOURCE_ERROR]);
	}
}

static void entropy_transition_next_locked(void)
{
	uint8_t next = entropy_next_source_locked();
	const struct entropy_source_ops *ops = entropy_source_get(next);

	smf_set_state(SMF_CTX(&entropy),
		      ops == NULL ? &entropy_states[ENTROPY_SMF_READY] : ops->state);
}

static int entropy_complete_source_locked(uint32_t duration_ms)
{
	if (entropy_progress(entropy.current, entropy.units) != 1000U) {
		return 0;
	}

	struct entropy_record *record = &entropy.records[POPCOUNT(entropy.completed)];
	size_t digest_len = 0U;
	psa_status_t status =
		psa_hash_finish(&entropy.hash, record->digest, sizeof(record->digest), &digest_len);

	entropy.hash = psa_hash_operation_init();
	entropy.hash_active = false;
	if (status != PSA_SUCCESS || digest_len != sizeof(record->digest)) {
		explicit_bzero(record, sizeof(*record));
		return -EIO;
	}

	record->source = entropy.current;
	record->units = entropy.units;
	record->duration_ms = duration_ms;
	entropy.completed |= entropy.current;
	return 1;
}

static enum smf_state_result entropy_source_run(void *object)
{
	struct entropy_context *context = object;

	if (context->event.kind != ENTROPY_EVENT_FEED) {
		return SMF_EVENT_PROPAGATE;
	}
	if (context->event.session != context->session ||
	    context->event.source != context->current) {
		context->event_result = -ECANCELED;
		return SMF_EVENT_HANDLED;
	}

	uint8_t metadata[8];
	uint32_t duration_ms = (uint32_t)(k_uptime_get() - context->started_ms);
	sys_put_le32(duration_ms, &metadata[0]);
	sys_put_le32(context->event.units, &metadata[4]);
	psa_status_t status = psa_hash_update(&context->hash, metadata, sizeof(metadata));

	if (status == PSA_SUCCESS) {
		status = psa_hash_update(&context->hash, context->event.data, context->event.len);
	}
	if (status != PSA_SUCCESS) {
		context->last_error = -EIO;
		context->event_result = -EIO;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_SOURCE_ERROR]);
		return SMF_EVENT_HANDLED;
	}

	context->units += context->event.units;
	int ret = entropy_complete_source_locked(duration_ms);

	if (ret < 0) {
		context->last_error = ret;
		context->event_result = ret;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_SOURCE_ERROR]);
	} else if (ret == 1) {
		context->event_result = 1;
		entropy_transition_next_locked();
	} else {
		context->event_result = 0;
	}
	return SMF_EVENT_HANDLED;
}

static enum smf_state_result entropy_error_run(void *object)
{
	struct entropy_context *context = object;

	if (context->event.kind != ENTROPY_EVENT_RETRY) {
		return SMF_EVENT_PROPAGATE;
	}
	if (context->event.session != context->session) {
		context->event_result = -EINVAL;
		return SMF_EVENT_HANDLED;
	}
	const struct entropy_source_ops *ops = entropy_source_get(context->current);

	if (ops == NULL) {
		context->event_result = -EINVAL;
		return SMF_EVENT_HANDLED;
	}
	context->event_result = 0;
	smf_set_state(SMF_CTX(context), ops->state);
	return SMF_EVENT_HANDLED;
}

static enum smf_state_result entropy_active_run(void *object)
{
	struct entropy_context *context = object;

	if (context->event.session != context->session) {
		return SMF_EVENT_HANDLED;
	}

	switch (context->event.kind) {
	case ENTROPY_EVENT_FAIL:
		if (entropy_state_locked() != APP_ENTROPY_CAPTURING ||
		    context->event.source != context->current) {
			context->event_result = -ECANCELED;
			break;
		}
		context->last_error = context->event.error;
		context->event_result = 0;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_SOURCE_ERROR]);
		break;
	case ENTROPY_EVENT_SKIP:
		if (context->current == APP_ENTROPY_SOURCE_NONE) {
			context->event_result = -EINVAL;
			break;
		}
		context->skipped |= context->current;
		context->event_result = 0;
		entropy_transition_next_locked();
		break;
	case ENTROPY_EVENT_CANCEL:
		context->event_result = 0;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_IDLE]);
		break;
	default:
		return SMF_EVENT_PROPAGATE;
	}
	return SMF_EVENT_HANDLED;
}

static enum smf_state_result entropy_idle_run(void *object)
{
	struct entropy_context *context = object;

	if (context->event.kind == ENTROPY_EVENT_BEGIN) {
		context->event_result = 0;
		entropy_transition_next_locked();
	}
	return SMF_EVENT_HANDLED;
}

static enum smf_state_result entropy_ready_run(void *object)
{
	struct entropy_context *context = object;

	if (context->event.kind == ENTROPY_EVENT_CANCEL &&
	    context->event.session == context->session) {
		context->event_result = 0;
		smf_set_state(SMF_CTX(context), &entropy_states[ENTROPY_SMF_IDLE]);
	}
	return SMF_EVENT_HANDLED;
}

static const struct smf_state entropy_states[] = {
	[ENTROPY_SMF_IDLE] =
		SMF_CREATE_STATE(entropy_idle_entry, entropy_idle_run, NULL, NULL, NULL),
	[ENTROPY_SMF_ACTIVE] = SMF_CREATE_STATE(NULL, entropy_active_run, NULL, NULL, NULL),
	[ENTROPY_SMF_TOUCH] =
		SMF_CREATE_STATE(entropy_source_entry, entropy_source_run, entropy_source_exit,
				 &entropy_states[ENTROPY_SMF_ACTIVE], NULL),
	[ENTROPY_SMF_IMU] =
		SMF_CREATE_STATE(entropy_source_entry, entropy_source_run, entropy_source_exit,
				 &entropy_states[ENTROPY_SMF_ACTIVE], NULL),
	[ENTROPY_SMF_CAMERA] =
		SMF_CREATE_STATE(entropy_source_entry, entropy_source_run, entropy_source_exit,
				 &entropy_states[ENTROPY_SMF_ACTIVE], NULL),
	[ENTROPY_SMF_MICROPHONE] =
		SMF_CREATE_STATE(entropy_source_entry, entropy_source_run, entropy_source_exit,
				 &entropy_states[ENTROPY_SMF_ACTIVE], NULL),
	[ENTROPY_SMF_SOURCE_ERROR] = SMF_CREATE_STATE(entropy_error_entry, entropy_error_run, NULL,
						      &entropy_states[ENTROPY_SMF_ACTIVE], NULL),
	[ENTROPY_SMF_READY] =
		SMF_CREATE_STATE(entropy_ready_entry, entropy_ready_run, NULL, NULL, NULL),
};

static void entropy_initialize_locked(void)
{
	if (SMF_CTX(&entropy)->current == NULL) {
		smf_set_initial(SMF_CTX(&entropy), &entropy_states[ENTROPY_SMF_IDLE]);
	}
}

static int entropy_dispatch_locked(struct entropy_event event)
{
	entropy.event = event;
	entropy.event_result = event.kind == ENTROPY_EVENT_FEED || event.kind == ENTROPY_EVENT_FAIL
				       ? -ECANCELED
				       : -EINVAL;
	(void)smf_run_state(SMF_CTX(&entropy));
	entropy.event.kind = ENTROPY_EVENT_NONE;
	return entropy.event_result;
}

static int entropy_dispatch(struct entropy_event event)
{
	k_mutex_lock(&entropy_lock, K_FOREVER);
	entropy_initialize_locked();
	int ret = entropy_dispatch_locked(event);
	k_mutex_unlock(&entropy_lock);
	return ret;
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
	entropy_initialize_locked();
	if (entropy_state_locked() != APP_ENTROPY_IDLE) {
		k_mutex_unlock(&entropy_lock);
		return -EBUSY;
	}

	uint32_t next_session = entropy.session + 1U;
	if (next_session == 0U) {
		next_session = 1U;
	}
	entropy.session = next_session;
	entropy.words = words;
	entropy.selected = selected;
	*session = next_session;
	int ret = entropy_dispatch_locked((struct entropy_event){
		.kind = ENTROPY_EVENT_BEGIN,
		.session = next_session,
	});
	k_mutex_unlock(&entropy_lock);
	return ret;
}

int app_entropy_feed(uint32_t session, enum app_entropy_source source, const void *data, size_t len,
		     uint32_t units)
{
	if (data == NULL || len == 0U || units == 0U) {
		return -EINVAL;
	}

	return entropy_dispatch((struct entropy_event){
		.data = data,
		.len = len,
		.session = session,
		.units = units,
		.source = (uint8_t)source,
		.kind = ENTROPY_EVENT_FEED,
	});
}

int app_entropy_fail(uint32_t session, enum app_entropy_source source, int error)
{
	return entropy_dispatch((struct entropy_event){
		.session = session,
		.error = error,
		.source = (uint8_t)source,
		.kind = ENTROPY_EVENT_FAIL,
	});
}

int app_entropy_retry(uint32_t session)
{
	return entropy_dispatch((struct entropy_event){
		.session = session,
		.kind = ENTROPY_EVENT_RETRY,
	});
}

int app_entropy_skip(uint32_t session)
{
	return entropy_dispatch((struct entropy_event){
		.session = session,
		.kind = ENTROPY_EVENT_SKIP,
	});
}

int app_entropy_snapshot_get(struct app_entropy_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&entropy_lock, K_FOREVER);
	entropy_initialize_locked();
	*snapshot = (struct app_entropy_snapshot){
		.session = entropy.session,
		.progress_permille = entropy_progress(entropy.current, entropy.units),
		.completed = entropy.completed,
		.skipped = entropy.skipped,
		.current = entropy.current,
		.state = entropy_state_locked(),
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
	entropy_initialize_locked();
	uint8_t record_count = POPCOUNT(entropy.completed);
	size_t required =
		ENTROPY_TRANSCRIPT_HEADER_SIZE + record_count * ENTROPY_TRANSCRIPT_RECORD_SIZE;
	if (entropy.session != session || entropy_state_locked() != APP_ENTROPY_READY) {
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
	(void)entropy_dispatch((struct entropy_event){
		.session = session,
		.kind = ENTROPY_EVENT_CANCEL,
	});
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
		int64_t next_sample_ms = 0;
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

			int feed_ret = 0;
			int64_t now = k_uptime_get();
			if (now >= next_sample_ms) {
				size_t frame_size = MIN(frame->bytesused, (size_t)format.size);
				feed_ret = app_entropy_feed(snapshot.session,
							    APP_ENTROPY_SOURCE_CAMERA,
							    frame->buffer, frame_size, 1U);
				next_sample_ms = now + ENTROPY_CAMERA_SAMPLE_INTERVAL_MS;
			}
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
