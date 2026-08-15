/* SPDX-License-Identifier: MPL-2.0 */

#include "ui.h"

#if defined(CONFIG_OSKEY_RUST)

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"
#include "entropy/entropy.h"

static lv_timer_t *entropy_timer;
static lv_obj_t *entropy_progress_label;
static struct app_entropy_snapshot rendered_snapshot;
static bool touch_point_valid;
static lv_point_t touch_last_point;

static const void *entropy_source_icon(uint8_t source)
{
	switch (source) {
	case APP_ENTROPY_SOURCE_TOUCH:
		return &oskey_shuffle;
	case APP_ENTROPY_SOURCE_IMU:
		return &oskey_imu;
	case APP_ENTROPY_SOURCE_CAMERA:
		return &oskey_camera;
	case APP_ENTROPY_SOURCE_MICROPHONE:
		return &oskey_microphone;
	default:
		return &oskey_warning;
	}
}

static const char *entropy_source_name(uint8_t source)
{
	switch (source) {
	case APP_ENTROPY_SOURCE_TOUCH:
		return "Screen movement";
	case APP_ENTROPY_SOURCE_IMU:
		return "Device movement";
	case APP_ENTROPY_SOURCE_CAMERA:
		return "Camera frames";
	case APP_ENTROPY_SOURCE_MICROPHONE:
		return "Ambient sound";
	default:
		return "Unknown source";
	}
}

static const char *entropy_source_instruction(uint8_t source)
{
	switch (source) {
	case APP_ENTROPY_SOURCE_TOUCH:
		return "Draw continuously across the area below";
	case APP_ENTROPY_SOURCE_IMU:
		return "Move and rotate the device in different directions";
	case APP_ENTROPY_SOURCE_CAMERA:
		return "Point the camera at a changing scene";
	case APP_ENTROPY_SOURCE_MICROPHONE:
		return "Make varied sounds near the device";
	default:
		return "Collecting";
	}
}

static void generate_standard(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_submit(LocalRequestKind_GenerateMnemonic, ui.mnemonic_words, NULL, 0, NULL, 0);
}

static void enter_custom_entropy(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_wipe(ui.entropy, sizeof(ui.entropy));
	ui_push(UI_PAGE_ENTROPY);
}

static void enter_mixed_entropy(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui.entropy_sources = app_entropy_capabilities() & APP_ENTROPY_SOURCE_AUXILIARY_MASK;
	ui_push(UI_PAGE_ENTROPY_SOURCES);
}

void ui_entropy_method_render(void)
{
	lv_obj_t *content = ui_page_begin("Entropy method", UI_NAVIGATION_BACK);
	uint8_t capabilities = app_entropy_capabilities();
	uint8_t auxiliary = capabilities & APP_ENTROPY_SOURCE_AUXILIARY_MASK;

	ui_list_row(content, &oskey_shuffle, "Choose how randomness is created",
		    "The recovery phrase is the wallet backup", NULL, UI_TONE_ACTIVE, NULL, NULL);
	ui_section(content, "METHOD");
	ui_list_row(content, &oskey_wallet, "Hardware randomness", "Recommended secure default",
		    NULL, UI_TONE_SUCCESS, generate_standard, NULL);
	if ((capabilities & APP_ENTROPY_SOURCE_HARDWARE_RNG) != 0U && auxiliary != 0U) {
		ui_list_row(content, &oskey_shuffle, "Multi-source enhanced",
			    "Mix hardware randomness with selected sensors", NULL, UI_TONE_ACTIVE,
			    enter_mixed_entropy, NULL);
	}
	ui_list_row(content, &oskey_document, "Exact custom entropy",
		    "Enter every bit directly; no random mixing", NULL, UI_TONE_WARNING,
		    enter_custom_entropy, NULL);
}

static void entropy_source_toggle(lv_event_t *event)
{
	uint8_t source = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
	ui.entropy_sources ^= source;
	ui_refresh();
}

static void entropy_collection_begin(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (ui.entropy_sources == 0U) {
		ui_error("Select at least one additional source");
		return;
	}

	uint32_t session;
	int ret = app_entropy_begin(ui.mnemonic_words, ui.entropy_sources, &session);
	if (ret < 0) {
		ui_error(ret == -EBUSY ? "Entropy collector is busy" : "Sources are unavailable");
		return;
	}
	ui.entropy_session = session;
	ui_push(UI_PAGE_ENTROPY_COLLECT);
}

void ui_entropy_sources_render(void)
{
	lv_obj_t *content = ui_page_begin("Additional sources", UI_NAVIGATION_BACK);
	uint8_t available = app_entropy_capabilities() & APP_ENTROPY_SOURCE_AUXILIARY_MASK;
	ui.entropy_sources &= available;

	ui_list_row(content, &oskey_success, "Hardware randomness", "Always included in this mode",
		    "On", UI_TONE_SUCCESS, NULL, NULL);
	ui_section(content, "SELECT SOURCES");
	for (uint8_t source = APP_ENTROPY_SOURCE_TOUCH; source <= APP_ENTROPY_SOURCE_MICROPHONE;
	     source <<= 1) {
		if ((available & source) == 0U) {
			continue;
		}
		bool selected = (ui.entropy_sources & source) != 0U;
		ui_list_row(content, entropy_source_icon(source), entropy_source_name(source),
			    selected ? "Included in the final mix" : "Not included",
			    selected ? "On" : "Off", selected ? UI_TONE_ACTIVE : UI_TONE_MUTED,
			    entropy_source_toggle, (void *)(uintptr_t)source);
	}
	ui_section(content, "CONTINUE");
	ui_list_row(content, &oskey_chevron_right, "Collect selected sources",
		    "Raw sensor data is not saved", NULL,
		    ui.entropy_sources != 0U ? UI_TONE_ACTIVE : UI_TONE_MUTED,
		    ui.entropy_sources != 0U ? entropy_collection_begin : NULL, NULL);
}

static void entropy_tick(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	struct app_entropy_snapshot snapshot;

	if (app_entropy_snapshot_get(&snapshot) < 0 || snapshot.session != ui.entropy_session) {
		return;
	}
	if (snapshot.state != rendered_snapshot.state ||
	    snapshot.current != rendered_snapshot.current ||
	    snapshot.completed != rendered_snapshot.completed ||
	    snapshot.skipped != rendered_snapshot.skipped) {
		ui_refresh();
		return;
	}
	if (entropy_progress_label != NULL &&
	    snapshot.progress_permille != rendered_snapshot.progress_permille) {
		lv_label_set_text_fmt(entropy_progress_label, "%u%% complete",
				      snapshot.progress_permille / 10U);
		rendered_snapshot.progress_permille = snapshot.progress_permille;
	}
}

static void entropy_timer_stop(void)
{
	if (entropy_timer != NULL) {
		lv_timer_delete(entropy_timer);
		entropy_timer = NULL;
	}
	entropy_progress_label = NULL;
}

static void entropy_touch_event(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);
	if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
		return;
	}

	lv_indev_t *indev = lv_indev_active();
	if (indev == NULL) {
		return;
	}
	lv_point_t point;
	lv_indev_get_point(indev, &point);
	if (touch_point_valid && point.x == touch_last_point.x && point.y == touch_last_point.y) {
		return;
	}

	int32_t dx = touch_point_valid ? point.x - touch_last_point.x : 0;
	int32_t dy = touch_point_valid ? point.y - touch_last_point.y : 0;
	uint8_t sample[8];
	sys_put_le16((uint16_t)CLAMP(point.x, INT16_MIN, INT16_MAX), &sample[0]);
	sys_put_le16((uint16_t)CLAMP(point.y, INT16_MIN, INT16_MAX), &sample[2]);
	sys_put_le16((uint16_t)CLAMP(dx, INT16_MIN, INT16_MAX), &sample[4]);
	sys_put_le16((uint16_t)CLAMP(dy, INT16_MIN, INT16_MAX), &sample[6]);
	(void)app_entropy_feed(ui.entropy_session, APP_ENTROPY_SOURCE_TOUCH, sample, sizeof(sample),
			       1U);
	touch_last_point = point;
	touch_point_valid = true;
}

static void entropy_retry_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (app_entropy_retry(ui.entropy_session) < 0) {
		ui_error("Could not retry this source");
		return;
	}
	ui_refresh();
}

static void entropy_skip_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (app_entropy_skip(ui.entropy_session) < 0) {
		ui_error("Could not skip this source");
		return;
	}
	ui_refresh();
}

static void entropy_generate_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	uint8_t transcript[APP_ENTROPY_TRANSCRIPT_MAX_SIZE];
	size_t transcript_len = 0U;
	int ret = app_entropy_transcript_get(ui.entropy_session, transcript, sizeof(transcript),
					     &transcript_len);
	if (ret < 0) {
		ui_error("Entropy result is unavailable");
		ui_wipe(transcript, sizeof(transcript));
		return;
	}

	if (ui_submit(LocalRequestKind_GenerateMnemonicMixed, ui.mnemonic_words, transcript,
		      transcript_len, NULL, 0) < 0) {
		ui_wipe(transcript, sizeof(transcript));
		return;
	}

	entropy_timer_stop();
	ui_wipe(transcript, sizeof(transcript));
	app_entropy_cancel(ui.entropy_session);
	ui.entropy_session = 0U;
}

static void entropy_render_completed(lv_obj_t *content, const struct app_entropy_snapshot *snapshot)
{
	uint8_t handled = snapshot->completed | snapshot->skipped;
	if (handled == 0U) {
		return;
	}

	ui_section(content, "COLLECTED");
	for (uint8_t source = APP_ENTROPY_SOURCE_TOUCH; source <= APP_ENTROPY_SOURCE_MICROPHONE;
	     source <<= 1) {
		if ((snapshot->completed & source) != 0U) {
			ui_list_row(content, &oskey_success, entropy_source_name(source),
				    "Collected", NULL, UI_TONE_SUCCESS, NULL, NULL);
		} else if ((snapshot->skipped & source) != 0U) {
			ui_list_row(content, &oskey_warning, entropy_source_name(source), "Skipped",
				    NULL, UI_TONE_WARNING, NULL, NULL);
		}
	}
}

void ui_entropy_collect_render(void)
{
	entropy_timer_stop();
	lv_obj_t *content = ui_page_begin("Enhanced randomness", UI_NAVIGATION_BACK);
	struct app_entropy_snapshot snapshot;

	if (app_entropy_snapshot_get(&snapshot) < 0 || snapshot.session != ui.entropy_session) {
		ui_list_row(content, &oskey_warning, "Collector unavailable", "Go back and retry",
			    NULL, UI_TONE_DANGER, NULL, NULL);
		return;
	}
	rendered_snapshot = snapshot;
	touch_point_valid = false;
	entropy_render_completed(content, &snapshot);

	if (snapshot.state == APP_ENTROPY_ERROR) {
		ui_section(content, "SOURCE ERROR");
		ui_list_row(content, &oskey_warning, entropy_source_name(snapshot.current),
			    "The source stopped before collection completed", NULL, UI_TONE_DANGER,
			    NULL, NULL);
		ui_list_row(content, &oskey_refresh, "Retry", "Collect this source again", NULL,
			    UI_TONE_ACTIVE, entropy_retry_clicked, NULL);
		ui_list_row(content, &oskey_chevron_right, "Skip",
			    "Hardware randomness remains active", NULL, UI_TONE_WARNING,
			    entropy_skip_clicked, NULL);
	} else if (snapshot.state == APP_ENTROPY_READY) {
		ui_section(content, "READY");
		ui_list_row(content, &oskey_success, "Sources collected",
			    "Mix them with fresh hardware randomness", NULL, UI_TONE_SUCCESS, NULL,
			    NULL);
		ui_list_row(content, &oskey_document, "Generate recovery phrase",
			    "Raw samples will be discarded", NULL, UI_TONE_ACTIVE,
			    entropy_generate_clicked, NULL);
	} else if (snapshot.state == APP_ENTROPY_CAPTURING) {
		ui_section(content, "CURRENT SOURCE");
		ui_list_row(content, entropy_source_icon(snapshot.current),
			    entropy_source_name(snapshot.current),
			    entropy_source_instruction(snapshot.current), NULL, UI_TONE_ACTIVE,
			    NULL, NULL);
		entropy_progress_label = lv_label_create(content);
		lv_obj_set_width(entropy_progress_label, LV_PCT(100));
		lv_obj_set_style_text_color(entropy_progress_label, ui_tone_color(UI_TONE_ACTIVE),
					    0);
		lv_obj_set_style_text_font(entropy_progress_label, UI_FONT_BODY, 0);
		lv_obj_set_style_text_align(entropy_progress_label, LV_TEXT_ALIGN_CENTER, 0);
		lv_label_set_text_fmt(entropy_progress_label, "%u%% complete",
				      snapshot.progress_permille / 10U);

		if (snapshot.current == APP_ENTROPY_SOURCE_TOUCH) {
			lv_obj_t *pad = lv_obj_create(content);
			lv_obj_set_size(pad, LV_PCT(100), 110);
			lv_obj_set_style_bg_color(pad, lv_color_hex(0x14181d), 0);
			lv_obj_set_style_border_color(pad, ui_tone_color(UI_TONE_ACTIVE), 0);
			lv_obj_set_style_border_width(pad, 1, 0);
			lv_obj_set_style_radius(pad, 8, 0);
			lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(pad, entropy_touch_event, LV_EVENT_ALL, NULL);
			lv_obj_t *hint = lv_label_create(pad);
			lv_label_set_text(hint, "Swipe here continuously");
			lv_obj_set_style_text_color(hint, ui_tone_color(UI_TONE_MUTED), 0);
			lv_obj_center(hint);
		}
		ui_list_row(content, &oskey_chevron_right, "Skip this source",
			    "Continue with hardware randomness", NULL, UI_TONE_WARNING,
			    entropy_skip_clicked, NULL);
	}

	if (snapshot.state == APP_ENTROPY_CAPTURING) {
		entropy_timer = lv_timer_create(entropy_tick, 100, NULL);
	}
}

void ui_entropy_collect_leave(void)
{
	entropy_timer_stop();
	if (ui.entropy_session != 0U) {
		app_entropy_cancel(ui.entropy_session);
		ui.entropy_session = 0U;
	}
	touch_point_valid = false;
}

#else

void ui_entropy_method_render(void)
{
	lv_obj_t *content = ui_page_begin("Entropy method", UI_NAVIGATION_BACK);
	ui_list_row(content, NULL, "Wallet unavailable", "Rust wallet support is disabled", NULL,
		    UI_TONE_MUTED, NULL, NULL);
}

void ui_entropy_sources_render(void)
{
	ui_entropy_method_render();
}

void ui_entropy_collect_render(void)
{
	ui_entropy_method_render();
}

void ui_entropy_collect_leave(void)
{
}

#endif
