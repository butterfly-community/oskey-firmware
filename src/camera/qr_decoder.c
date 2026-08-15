/* SPDX-License-Identifier: MPL-2.0 */

#include "qr_decoder.h"

#include <errno.h>
#include <limits.h>
#include <quirc.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/atomic.h>

#define QR_DECODER_PRIORITY 12

LOG_MODULE_REGISTER(qr_decoder, CONFIG_VIDEO_LOG_LEVEL);

enum qr_decoder_state {
	QR_DECODER_UNINITIALIZED,
	QR_DECODER_IDLE,
	QR_DECODER_READY,
	QR_DECODER_DECODING,
};

static K_SEM_DEFINE(qr_decoder_ready, 0, 1);
static struct quirc *qr_decoder;
static struct quirc_code qr_code;
static struct quirc_data qr_data;
static struct app_qr_code qr_result;
static uint8_t *qr_snapshot;
static size_t qr_snapshot_size;
static size_t qr_snapshot_pitch;
static app_qr_code_callback_t qr_callback;
static void *qr_callback_user_data;
static atomic_t qr_state = ATOMIC_INIT(QR_DECODER_UNINITIALIZED);
static int64_t qr_next_decode_ms;
static uint32_t qr_session;
static int qr_source_width;
static int qr_source_height;
static size_t qr_source_pitch;
static uint32_t qr_source_pixelformat;
static int qr_crop_x;
static int qr_crop_y;
static int qr_crop_width;
static int qr_crop_height;
static uint8_t qr_high_byte_index;
static uint8_t qr_low_byte_index;

static uint8_t rgb565_to_gray(const uint8_t *pixel)
{
	uint8_t green =
		((pixel[qr_high_byte_index] & 0x07U) << 3) | (pixel[qr_low_byte_index] >> 5);

	return (green << 2) | (green >> 4);
}

static void qr_decoder_publish(const struct quirc_data *data, uint32_t elapsed_ms, uint32_t session)
{
	if (data->payload_len < 0 || data->payload_len > CONFIG_OSKEY_QR_MAX_PAYLOAD_SIZE) {
		LOG_WRN("Invalid QR payload length: %d", data->payload_len);
		return;
	}

	memcpy(qr_result.payload, data->payload, data->payload_len);
	qr_result.payload_len = data->payload_len;
	qr_result.decode_time_ms = elapsed_ms;
	qr_result.version = data->version;
	qr_result.ecc_level = data->ecc_level;
	qr_callback(&qr_result, session, qr_callback_user_data);
	memset(&qr_result, 0, sizeof(qr_result));
}

static void qr_decoder_thread(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		k_sem_take(&qr_decoder_ready, K_FOREVER);
		if (!atomic_cas(&qr_state, QR_DECODER_READY, QR_DECODER_DECODING)) {
			continue;
		}

		uint32_t started = k_uptime_get_32();
		uint32_t session = qr_session;
		int image_width;
		int image_height;
		uint8_t *image = quirc_begin(qr_decoder, &image_width, &image_height);
		if (image == NULL || image_width != qr_crop_width ||
		    image_height != qr_crop_height) {
			LOG_ERR("Unable to begin QR frame: %p, %dx%d", image, image_width,
				image_height);
			atomic_set(&qr_state, QR_DECODER_IDLE);
			continue;
		}

		for (int y = 0; y < qr_crop_height; y++) {
			const uint8_t *row = qr_snapshot + (size_t)y * qr_snapshot_pitch;
			uint8_t *gray = image + (size_t)y * qr_crop_width;

			for (int x = 0; x < qr_crop_width; x++) {
				gray[x] = rgb565_to_gray(row + (size_t)x * 2U);
			}
		}
		quirc_end(qr_decoder);
		int count = quirc_count(qr_decoder);
		LOG_INF("QR frame: %u ms, %d candidate(s)", k_uptime_get_32() - started, count);

		for (int i = 0; i < count; i++) {
			quirc_extract(qr_decoder, i, &qr_code);
			quirc_decode_error_t error = quirc_decode(&qr_code, &qr_data);
			if (error == QUIRC_ERROR_DATA_ECC) {
				quirc_flip(&qr_code);
				error = quirc_decode(&qr_code, &qr_data);
			}
			if (error == QUIRC_SUCCESS) {
				qr_decoder_publish(&qr_data, k_uptime_get_32() - started, session);
			}
		}

		atomic_set(&qr_state, QR_DECODER_IDLE);
	}
}

int app_qr_decoder_init(const struct video_format *format, app_qr_code_callback_t callback,
			void *user_data)
{
	int source_width;
	int source_height;
	int crop_width;
	int crop_height;
	int crop_x;
	int crop_y;

	if (format == NULL || callback == NULL ||
	    (format->pixelformat != VIDEO_PIX_FMT_RGB565X &&
	     format->pixelformat != VIDEO_PIX_FMT_RGB565) ||
	    format->width == 0U || format->width > INT_MAX || format->height == 0U ||
	    format->height > INT_MAX || format->pitch / 2U < format->width ||
	    format->height > SIZE_MAX / format->width ||
	    format->height > SIZE_MAX / format->pitch ||
	    format->size < (size_t)format->pitch * format->height) {
		return -EINVAL;
	}

	source_width = (int)format->width;
	source_height = (int)format->height;
	crop_width = source_width - source_width / 4;
	crop_height = source_height - source_height / 4;
	crop_x = (source_width - crop_width) / 2;
	crop_y = (source_height - crop_height) / 2;
	if (crop_width < 1 || crop_height < 1) {
		return -EINVAL;
	}
	if (!atomic_cas(&qr_state, QR_DECODER_UNINITIALIZED, QR_DECODER_IDLE)) {
		return -EALREADY;
	}

	qr_source_width = source_width;
	qr_source_height = source_height;
	qr_source_pixelformat = format->pixelformat;
	qr_high_byte_index = format->pixelformat == VIDEO_PIX_FMT_RGB565X ? 0U : 1U;
	qr_low_byte_index = 1U - qr_high_byte_index;
	qr_crop_width = crop_width;
	qr_crop_height = crop_height;
	qr_crop_x = crop_x;
	qr_crop_y = crop_y;
	qr_source_pitch = format->pitch;
	qr_snapshot_pitch = (size_t)qr_crop_width * 2U;
	if ((size_t)qr_crop_height > SIZE_MAX / qr_snapshot_pitch) {
		atomic_set(&qr_state, QR_DECODER_UNINITIALIZED);
		return -EINVAL;
	}
	qr_snapshot_size = qr_snapshot_pitch * (size_t)qr_crop_height;
	qr_callback = callback;
	qr_callback_user_data = user_data;
	qr_next_decode_ms = 0;
	qr_decoder = quirc_new();
	if (qr_decoder == NULL || quirc_resize(qr_decoder, qr_crop_width, qr_crop_height) < 0) {
		if (qr_decoder != NULL) {
			quirc_destroy(qr_decoder);
			qr_decoder = NULL;
		}
		atomic_set(&qr_state, QR_DECODER_UNINITIALIZED);
		return -ENOMEM;
	}
	qr_snapshot = shared_multi_heap_aligned_alloc(
		SMH_REG_ATTR_EXTERNAL, CONFIG_VIDEO_BUFFER_POOL_ALIGN, qr_snapshot_size);
	if (qr_snapshot == NULL) {
		quirc_destroy(qr_decoder);
		qr_decoder = NULL;
		atomic_set(&qr_state, QR_DECODER_UNINITIALIZED);
		return -ENOMEM;
	}
	return 0;
}

int app_qr_decoder_submit(const struct video_buffer *buffer, const struct video_format *format,
			  uint32_t session)
{
	int64_t now = k_uptime_get();

	if (buffer == NULL || format == NULL || buffer->buffer == NULL ||
	    format->pixelformat != qr_source_pixelformat ||
	    format->width != (uint32_t)qr_source_width ||
	    format->height != (uint32_t)qr_source_height || format->pitch != qr_source_pitch ||
	    buffer->bytesused < (size_t)format->pitch * format->height) {
		return -EINVAL;
	}
	if (now < qr_next_decode_ms || !atomic_cas(&qr_state, QR_DECODER_IDLE, QR_DECODER_READY)) {
		return 0;
	}

	const uint8_t *source =
		buffer->buffer + (size_t)qr_crop_y * format->pitch + (size_t)qr_crop_x * 2U;
	for (int y = 0; y < qr_crop_height; y++) {
		memcpy(qr_snapshot + (size_t)y * qr_snapshot_pitch,
		       source + (size_t)y * format->pitch, qr_snapshot_pitch);
	}

	qr_session = session;
	qr_next_decode_ms = now + CONFIG_OSKEY_QR_DECODE_INTERVAL_MS;
	k_sem_give(&qr_decoder_ready);
	return 1;
}

K_THREAD_DEFINE(qr_decoder_thread_id, CONFIG_OSKEY_QR_THREAD_STACK_SIZE, qr_decoder_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(QR_DECODER_PRIORITY), 0, 0);
