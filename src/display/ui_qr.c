/* SPDX-License-Identifier: MPL-2.0 */

#include "ui.h"

#include "camera/qr_scanner.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/multi_heap/shared_multi_heap.h>

LOG_MODULE_REGISTER(ui_qr, CONFIG_LOG_DEFAULT_LEVEL);

static lv_obj_t *preview_object;
static lv_obj_t *status_label;
static lv_obj_t *result_label;
static lv_timer_t *preview_timer;
static uint8_t *preview_buffer;
static size_t preview_buffer_size;
static uint32_t preview_generation;
static uint32_t scanner_session;
static struct video_format preview_format;
static lv_image_dsc_t preview_descriptor;
static struct app_qr_code scanner_result;

static void preview_tick(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	uint32_t generation = preview_generation;
	int ret;

	if (preview_object == NULL) {
		return;
	}
	ret = app_qr_scanner_frame_copy(preview_buffer, preview_buffer_size, &generation);
	if (ret < 0 || generation == preview_generation) {
		if (ret < 0 && ret != -EAGAIN) {
			LOG_WRN("Preview copy failed: %d", ret);
		}
		return;
	}

	preview_generation = generation;
	lv_obj_invalidate(preview_object);
}

static int preview_prepare(void)
{
	struct video_format format;
	lv_color_format_t color_format;
	int ret = app_qr_scanner_get_format(&format);

	if (ret < 0) {
		return ret;
	}
	if ((format.pixelformat != VIDEO_PIX_FMT_RGB565X &&
	     format.pixelformat != VIDEO_PIX_FMT_RGB565) ||
	    format.width == 0U || format.height == 0U || format.pitch < format.width * 2U ||
	    format.size < format.pitch * format.height) {
		return -ENOTSUP;
	}
	color_format = format.pixelformat == VIDEO_PIX_FMT_RGB565X ? LV_COLOR_FORMAT_RGB565_SWAPPED
								   : LV_COLOR_FORMAT_RGB565;

	if (preview_buffer == NULL) {
		preview_buffer = shared_multi_heap_aligned_alloc(
			SMH_REG_ATTR_EXTERNAL, CONFIG_VIDEO_BUFFER_POOL_ALIGN, format.size);
		if (preview_buffer == NULL) {
			return -ENOMEM;
		}
		memset(preview_buffer, 0, format.size);
		preview_buffer_size = format.size;
		preview_format = format;
		preview_descriptor = (lv_image_dsc_t){
			.header =
				{
					.magic = LV_IMAGE_HEADER_MAGIC,
					.cf = color_format,
					.w = format.width,
					.h = format.height,
					.stride = format.pitch,
				},
			.data_size = format.size,
			.data = preview_buffer,
		};
	} else if (format.pixelformat != preview_format.pixelformat ||
		   format.width != preview_format.width || format.height != preview_format.height ||
		   format.pitch != preview_format.pitch || format.size != preview_format.size) {
		return -ENOTSUP;
	}

	if (preview_object == NULL) {
		preview_object = lv_image_create(ui.content);
		lv_image_set_src(preview_object, &preview_descriptor);
		lv_obj_center(preview_object);
	}
	if (preview_timer == NULL) {
		preview_generation = 0;
		preview_timer = lv_timer_create(preview_tick, 100, NULL);
		if (preview_timer == NULL) {
			return -ENOMEM;
		}
	}
	lv_obj_move_foreground(status_label);
	lv_obj_move_foreground(result_label);
	return 0;
}

static void result_text(const struct app_qr_code *code, char *buffer, size_t size)
{
	bool printable = code->payload_len > 0;
	for (size_t i = 0; i < code->payload_len; i++) {
		if (code->payload[i] < 0x20 || code->payload[i] > 0x7e) {
			printable = false;
			break;
		}
	}

	if (!printable) {
		snprintk(buffer, size, "Binary QR code (%u bytes)",
			 (unsigned int)code->payload_len);
		return;
	}

	size_t length = MIN(code->payload_len, size - 1U);
	memcpy(buffer, code->payload, length);
	buffer[length] = '\0';
}

void ui_qr_event(const struct app_qr_scanner_event *event)
{
	if (event == NULL || ui.page != UI_PAGE_QR_SCANNER || event->session != scanner_session) {
		return;
	}

	switch (event->state) {
	case APP_QR_SCANNER_STARTING:
		lv_label_set_text(status_label, "Starting camera");
		break;
	case APP_QR_SCANNER_RUNNING: {
		int ret = preview_prepare();

		if (ret < 0) {
			lv_label_set_text_fmt(status_label, "Preview unavailable (%d)", ret);
		} else {
			lv_label_set_text(status_label, "Scanning");
		}
	} break;
	case APP_QR_SCANNER_STOPPING:
		lv_label_set_text(status_label, "Stopping");
		break;
	case APP_QR_SCANNER_RESULT: {
		if (preview_timer != NULL) {
			lv_timer_delete(preview_timer);
			preview_timer = NULL;
		}
		if (app_qr_scanner_result_copy(event->session, &scanner_result) < 0) {
			lv_label_set_text(status_label, "Result unavailable");
			break;
		}
		char text[256];
		result_text(&scanner_result, text, sizeof(text));
		lv_label_set_text_fmt(status_label, "Decoded in %u ms",
				      scanner_result.decode_time_ms);
		lv_label_set_text(result_label, text);
		lv_obj_clear_flag(result_label, LV_OBJ_FLAG_HIDDEN);
		break;
	}
	case APP_QR_SCANNER_ERROR:
		lv_label_set_text_fmt(status_label, "Camera error (%d)", event->error);
		break;
	case APP_QR_SCANNER_STOPPED:
		lv_label_set_text(status_label, "Stopped");
		break;
	}
}

void ui_qr_render(void)
{
	lv_obj_t *content = ui_page_begin("", UI_NAVIGATION_BACK);
	lv_obj_set_layout(content, LV_LAYOUT_NONE);
	lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(content, 0, 0);

	preview_object = NULL;
	preview_generation = 0;
	status_label = lv_label_create(content);
	lv_obj_set_width(status_label, LV_PCT(100));
	lv_obj_set_style_bg_color(status_label, lv_color_hex(0x090b0e), 0);
	lv_obj_set_style_bg_opa(status_label, LV_OPA_80, 0);
	lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_all(status_label, 6, 0);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_label_set_text(status_label, "Starting camera");

	result_label = lv_label_create(content);
	lv_obj_set_width(result_label, LV_PCT(100));
	lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_bg_color(result_label, lv_color_hex(0x090b0e), 0);
	lv_obj_set_style_bg_opa(result_label, LV_OPA_90, 0);
	lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_all(result_label, 10, 0);
	lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(result_label, LV_OBJ_FLAG_HIDDEN);

	int ret = app_qr_scanner_start(&scanner_session);
	if (ret < 0) {
		lv_label_set_text_fmt(status_label, "Scanner unavailable (%d)", ret);
	}
}

void ui_qr_leave(void)
{
	if (preview_timer != NULL) {
		lv_timer_delete(preview_timer);
		preview_timer = NULL;
	}
	(void)app_qr_scanner_stop();
	preview_object = NULL;
	status_label = NULL;
	result_label = NULL;
	scanner_session = 0;
}
