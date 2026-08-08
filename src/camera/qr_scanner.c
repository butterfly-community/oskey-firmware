/* SPDX-License-Identifier: Apache-2.0 */

#include "qr_scanner.h"

#include "camera.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(qr_scanner, CONFIG_VIDEO_LOG_LEVEL);

#define PREVIEW_INTERVAL_MS 100

ZBUS_CHAN_DEFINE(app_qr_scanner_event_chan, struct app_qr_scanner_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = APP_QR_SCANNER_STOPPED, .session = 0, .error = 0));

static K_SEM_DEFINE(scanner_control, 0, 1);
static K_MUTEX_DEFINE(scanner_lock);
static K_MUTEX_DEFINE(preview_lock);
static K_MUTEX_DEFINE(result_lock);

static atomic_t scanner_requested;
static atomic_t scanner_state = ATOMIC_INIT(APP_QR_SCANNER_STOPPED);
static atomic_t scanner_session;
static uint8_t *preview_snapshot;
static size_t preview_snapshot_size;
static uint32_t preview_generation;
static struct video_format scanner_format;
static bool decoder_initialized;
static uint32_t result_session;
static struct app_qr_code result_code;

static void scanner_publish(enum app_qr_scanner_state state, uint32_t session, int error)
{
	struct app_qr_scanner_event event = {
		.state = state,
		.session = session,
		.error = error,
	};

	atomic_set(&scanner_state, state);
	if (zbus_chan_pub(&app_qr_scanner_event_chan, &event, K_FOREVER) < 0) {
		LOG_WRN("Unable to publish scanner state %d", state);
	}
}

static void scanner_qr_found(const struct app_qr_code *code, uint32_t session, void *user_data)
{
	ARG_UNUSED(user_data);

	if (session != (uint32_t)atomic_get(&scanner_session) ||
	    atomic_get(&scanner_requested) == 0) {
		return;
	}

	LOG_INF("Decoded QR payload: %u bytes in %u ms", (unsigned int)code->payload_len,
		code->decode_time_ms);
	k_mutex_lock(&result_lock, K_FOREVER);
	if (session != (uint32_t)atomic_get(&scanner_session) ||
	    atomic_get(&scanner_requested) == 0) {
		k_mutex_unlock(&result_lock);
		return;
	}
	result_session = session;
	result_code = *code;
	atomic_set(&scanner_requested, 0);
	k_mutex_unlock(&result_lock);
}

static int scanner_prepare(const struct video_format *format)
{
	if (preview_snapshot != NULL) {
		if (format->width != scanner_format.width ||
		    format->height != scanner_format.height ||
		    format->pitch != scanner_format.pitch || format->size != scanner_format.size ||
		    format->pixelformat != scanner_format.pixelformat) {
			return -ENOTSUP;
		}
	} else {
		preview_snapshot = shared_multi_heap_aligned_alloc(
			SMH_REG_ATTR_EXTERNAL, CONFIG_VIDEO_BUFFER_POOL_ALIGN, format->size);
		if (preview_snapshot == NULL) {
			LOG_ERR("Preview snapshot allocation failed: %zu bytes", format->size);
			return -ENOMEM;
		}
		preview_snapshot_size = format->size;
		scanner_format = *format;
	}

	return 0;
}

static int scanner_decoder_start(const struct video_format *format)
{
	if (decoder_initialized) {
		return 0;
	}

	int ret = app_qr_decoder_init(format, scanner_qr_found, NULL);
	if (ret == 0 || ret == -EALREADY) {
		decoder_initialized = true;
		return 0;
	}

	LOG_WRN("QR decoder unavailable: %d", ret);
	return ret;
}

static int scanner_capture(uint32_t session)
{
	struct video_format format;
	int ret = app_camera_start(&format);

	if (ret < 0) {
		return ret;
	}

	ret = scanner_prepare(&format);
	if (ret < 0) {
		(void)app_camera_stop();
		return ret;
	}
	ret = scanner_decoder_start(&format);
	if (ret < 0) {
		(void)app_camera_stop();
		return ret;
	}
	k_mutex_lock(&preview_lock, K_FOREVER);
	preview_generation = 0;
	k_mutex_unlock(&preview_lock);

	LOG_INF("Scanner session %u started: %ux%u pitch=%u size=%u fmt=0x%08x", session,
		format.width, format.height, format.pitch, format.size, format.pixelformat);
	scanner_publish(APP_QR_SCANNER_RUNNING, session, 0);
	int64_t next_preview_ms = 0;
	while (atomic_get(&scanner_requested) != 0 &&
	       session == (uint32_t)atomic_get(&scanner_session)) {
		struct video_buffer *buffer;

		ret = app_camera_frame_get(&buffer, K_MSEC(200));
		if (ret == -EAGAIN) {
			continue;
		}
		if (ret < 0) {
			break;
		}
		int64_t now = k_uptime_get();
		if (now >= next_preview_ms) {
			k_mutex_lock(&preview_lock, K_FOREVER);
			memcpy(preview_snapshot, buffer->buffer, preview_snapshot_size);
			preview_generation++;
			k_mutex_unlock(&preview_lock);
			next_preview_ms = now + PREVIEW_INTERVAL_MS;
		}

		(void)app_qr_decoder_submit(buffer, &format, session);
		ret = app_camera_frame_release(buffer);
		if (ret < 0) {
			LOG_ERR("Camera frame release failed: %d", ret);
			break;
		}
	}

	int stop_ret = app_camera_stop();
	LOG_INF("Scanner session %u stopped", session);
	return ret < 0 ? ret : stop_ret;
}

static void scanner_thread(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		k_sem_take(&scanner_control, K_FOREVER);
		uint32_t session = (uint32_t)atomic_get(&scanner_session);

		if (atomic_get(&scanner_requested) == 0) {
			scanner_publish(APP_QR_SCANNER_STOPPED, session, 0);
			continue;
		}

		int ret = scanner_capture(session);
		atomic_set(&scanner_requested, 0);
		if (ret < 0) {
			LOG_ERR("Scanner stopped with error: %d", ret);
			scanner_publish(APP_QR_SCANNER_ERROR, session, ret);
		} else {
			bool result;

			k_mutex_lock(&result_lock, K_FOREVER);
			result = result_session == session;
			k_mutex_unlock(&result_lock);
			scanner_publish(result ? APP_QR_SCANNER_RESULT : APP_QR_SCANNER_STOPPED,
					       session, 0);
		}
	}
}

int app_qr_scanner_start(uint32_t *session_out)
{
	if (session_out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&scanner_lock, K_FOREVER);
	enum app_qr_scanner_state state = atomic_get(&scanner_state);
	if (state == APP_QR_SCANNER_STARTING || state == APP_QR_SCANNER_RUNNING ||
	    state == APP_QR_SCANNER_STOPPING) {
		k_mutex_unlock(&scanner_lock);
		return -EALREADY;
	}

	uint32_t session = (uint32_t)atomic_inc(&scanner_session) + 1U;
	*session_out = session;
	k_mutex_lock(&result_lock, K_FOREVER);
	result_session = 0;
	memset(&result_code, 0, sizeof(result_code));
	k_mutex_unlock(&result_lock);
	atomic_set(&scanner_requested, 1);
	scanner_publish(APP_QR_SCANNER_STARTING, session, 0);
	k_sem_give(&scanner_control);
	k_mutex_unlock(&scanner_lock);
	return 0;
}

int app_qr_scanner_stop(void)
{
	k_mutex_lock(&scanner_lock, K_FOREVER);
	enum app_qr_scanner_state state = atomic_get(&scanner_state);
	uint32_t session = (uint32_t)atomic_get(&scanner_session);

	if (state == APP_QR_SCANNER_STOPPED || state == APP_QR_SCANNER_ERROR) {
		k_mutex_unlock(&scanner_lock);
		return 0;
	}
	atomic_set(&scanner_requested, 0);
	if (state == APP_QR_SCANNER_STARTING || state == APP_QR_SCANNER_RUNNING) {
		scanner_publish(APP_QR_SCANNER_STOPPING, session, 0);
	}
	k_mutex_unlock(&scanner_lock);
	return 0;
}

int app_qr_scanner_get_format(struct video_format *format)
{
	if (format == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&preview_lock, K_FOREVER);
	if (preview_snapshot == NULL) {
		k_mutex_unlock(&preview_lock);
		return -EAGAIN;
	}
	*format = scanner_format;
	k_mutex_unlock(&preview_lock);
	return 0;
}

int app_qr_scanner_frame_copy(void *destination, size_t size, uint32_t *generation)
{
	if (destination == NULL || generation == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&preview_lock, K_FOREVER);
	if (preview_snapshot == NULL || preview_generation == 0U) {
		k_mutex_unlock(&preview_lock);
		return -EAGAIN;
	}
	if (size < preview_snapshot_size) {
		k_mutex_unlock(&preview_lock);
		return -ENOSPC;
	}
	if (*generation != preview_generation) {
		memcpy(destination, preview_snapshot, preview_snapshot_size);
		*generation = preview_generation;
	}
	k_mutex_unlock(&preview_lock);
	return 0;
}

int app_qr_scanner_result_copy(uint32_t session, struct app_qr_code *code)
{
	if (code == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&result_lock, K_FOREVER);
	if (session != result_session || atomic_get(&scanner_state) != APP_QR_SCANNER_RESULT) {
		k_mutex_unlock(&result_lock);
		return -ENOENT;
	}
	*code = result_code;
	k_mutex_unlock(&result_lock);
	return 0;
}

K_THREAD_DEFINE(qr_scanner_thread_id, CONFIG_OSKEY_QR_SCANNER_THREAD_STACK_SIZE, scanner_thread,
		NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);
