/* SPDX-License-Identifier: MPL-2.0 */

#define _DEFAULT_SOURCE

#include "ui.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"
#include "bus.h"

struct ui_context ui;

static struct {
	lv_style_t screen;
	lv_style_t content;
	lv_style_t title;
	lv_style_t text;
	lv_style_t muted;
	lv_style_t section;
	lv_style_t list;
} styles;

static void keyboard_show_deferred(void *input);
static void keyboard_hide_deferred(void *keyboard);
static void input_submit_deferred(void *input);

lv_color_t ui_tone_color(enum ui_tone tone)
{
	switch (tone) {
	case UI_TONE_MUTED:
		return lv_color_hex(0x697581);
	case UI_TONE_ACTIVE:
		return lv_color_hex(0x4da3ff);
	case UI_TONE_SUCCESS:
		return lv_color_hex(0x6fd6a4);
	case UI_TONE_WARNING:
		return lv_color_hex(0xe9b65e);
	case UI_TONE_DANGER:
		return lv_color_hex(0xe36a78);
	case UI_TONE_DEFAULT:
	default:
		return lv_color_hex(0xf2f5f7);
	}
}

static void notice_expired(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	lv_obj_add_flag(ui.notice, LV_OBJ_FLAG_HIDDEN);
	ui.notice_timer = NULL;
}

static void notice_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (ui.notice_timer != NULL) {
		lv_timer_delete(ui.notice_timer);
		ui.notice_timer = NULL;
	}
	lv_obj_add_flag(ui.notice, LV_OBJ_FLAG_HIDDEN);
}

static void theme_init(void)
{
	lv_style_init(&styles.screen);
	lv_style_set_bg_color(&styles.screen, lv_color_hex(0x090b0e));
	lv_style_set_bg_opa(&styles.screen, LV_OPA_COVER);
	lv_style_set_text_color(&styles.screen, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.screen, UI_FONT_BODY);
	lv_style_set_border_width(&styles.screen, 0);
	lv_style_set_pad_all(&styles.screen, 0);

	lv_style_init(&styles.content);
	lv_style_set_bg_opa(&styles.content, LV_OPA_TRANSP);
	lv_style_set_text_color(&styles.content, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.content, UI_FONT_BODY);
	lv_style_set_border_width(&styles.content, 0);
	lv_style_set_radius(&styles.content, 0);
	lv_style_set_pad_row(&styles.content, 8);

	lv_style_init(&styles.title);
	lv_style_set_text_color(&styles.title, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.title, UI_FONT_TITLE);
	lv_style_set_pad_top(&styles.title, 2);
	lv_style_set_pad_bottom(&styles.title, 4);

	lv_style_init(&styles.text);
	lv_style_set_text_color(&styles.text, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.text, UI_FONT_BODY);

	lv_style_init(&styles.muted);
	lv_style_set_text_color(&styles.muted, lv_color_hex(0x929eaa));
	lv_style_set_text_font(&styles.muted, &lv_font_montserrat_12);

	lv_style_init(&styles.section);
	lv_style_set_text_color(&styles.section, lv_color_hex(0x929eaa));
	lv_style_set_text_font(&styles.section, UI_FONT_CAPTION);
	lv_style_set_pad_top(&styles.section, 4);
	lv_style_set_pad_bottom(&styles.section, 1);

	lv_style_init(&styles.list);
	lv_style_set_bg_opa(&styles.list, LV_OPA_TRANSP);
	lv_style_set_border_side(&styles.list, LV_BORDER_SIDE_BOTTOM);
	lv_style_set_border_color(&styles.list, lv_color_hex(0x242b33));
	lv_style_set_border_width(&styles.list, 1);
	lv_style_set_radius(&styles.list, 0);
	lv_style_set_pad_hor(&styles.list, 4);
	lv_style_set_pad_ver(&styles.list, 4);
	lv_style_set_pad_column(&styles.list, 8);
	lv_style_set_shadow_width(&styles.list, 0);
}

static void content_bounds(void)
{
	lv_obj_set_size(ui.content, LV_PCT(100), ui.height - UI_STATUS_HEIGHT);
	lv_obj_align(ui.content, LV_ALIGN_TOP_MID, 0, UI_STATUS_HEIGHT);
}

void ui_init(const uint8_t features[APP_FEATURE_COUNT], const struct ui_status *status,
	     const struct app_wifi_config *wifi_config, const struct app_wifi_scan *wifi_scan)
{
	memset(&ui, 0, sizeof(ui));
	memcpy(ui.features, features, sizeof(ui.features));
	ui.status = *status;
	ui.wifi_config = *wifi_config;
	ui.wifi_scan = *wifi_scan;
	ui.screen = lv_screen_active();
	ui.width = lv_display_get_horizontal_resolution(NULL);
	ui.height = lv_display_get_vertical_resolution(NULL);

	theme_init();
	lv_obj_clean(ui.screen);
	lv_obj_add_style(ui.screen, &styles.screen, 0);
	lv_obj_set_scrollbar_mode(ui.screen, LV_SCROLLBAR_MODE_OFF);

	ui.content = lv_obj_create(ui.screen);
	lv_obj_add_style(ui.content, &styles.content, 0);
	content_bounds();
	lv_obj_set_flex_flow(ui.content, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ui.content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);
	lv_obj_set_scrollbar_mode(ui.content, LV_SCROLLBAR_MODE_ACTIVE);
	lv_obj_set_style_width(ui.content, 2, LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_color(ui.content, lv_color_hex(0x66727e), LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_opa(ui.content, LV_OPA_50, LV_PART_SCROLLBAR);
	lv_obj_set_style_radius(ui.content, 0, LV_PART_SCROLLBAR);
	lv_obj_set_style_pad_hor(ui.content, ui.width >= 480 ? 40 : 12, 0);
	lv_obj_set_style_pad_bottom(ui.content, 10, 0);

	ui.notice = lv_obj_create(ui.screen);
	lv_obj_set_width(ui.notice, LV_MIN(ui.width - 24, 456));
	lv_obj_set_height(ui.notice, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_color(ui.notice, lv_color_hex(0x12181e), 0);
	lv_obj_set_style_bg_opa(ui.notice, LV_OPA_COVER, 0);
	ui_press_feedback(ui.notice);
	lv_obj_set_style_border_color(ui.notice, ui_tone_color(UI_TONE_DANGER), 0);
	lv_obj_set_style_border_width(ui.notice, 1, 0);
	lv_obj_set_style_radius(ui.notice, 6, 0);
	lv_obj_set_style_shadow_width(ui.notice, 0, 0);
	lv_obj_set_style_pad_hor(ui.notice, 10, 0);
	lv_obj_set_style_pad_ver(ui.notice, 8, 0);
	lv_obj_set_style_pad_column(ui.notice, 8, 0);
	lv_obj_set_flex_flow(ui.notice, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(ui.notice, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_remove_flag(ui.notice, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(ui.notice, notice_clicked, LV_EVENT_CLICKED, NULL);

	lv_obj_t *notice_icon = ui_icon(ui.notice, &oskey_warning);
	ui_icon_color(notice_icon, ui_tone_color(UI_TONE_DANGER));
	ui.notice_label = lv_label_create(ui.notice);
	lv_obj_set_flex_grow(ui.notice_label, 1);
	lv_obj_set_style_text_color(ui.notice_label, ui_tone_color(UI_TONE_DANGER), 0);
	lv_obj_set_style_text_font(ui.notice_label, UI_FONT_BODY, 0);
	lv_label_set_long_mode(ui.notice_label, LV_LABEL_LONG_WRAP);
	lv_obj_add_flag(ui.notice, LV_OBJ_FLAG_HIDDEN);

	ui.busy = lv_obj_create(ui.screen);
	lv_obj_set_size(ui.busy, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(ui.busy, lv_color_hex(0x090b0e), 0);
	lv_obj_set_style_bg_opa(ui.busy, LV_OPA_60, 0);
	lv_obj_set_style_border_width(ui.busy, 0, 0);
	lv_obj_set_style_radius(ui.busy, 0, 0);
	lv_obj_set_style_pad_all(ui.busy, 0, 0);
	lv_obj_add_flag(ui.busy, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(ui.busy, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(ui.busy, LV_OBJ_FLAG_HIDDEN);

	lv_obj_t *spinner = lv_spinner_create(ui.busy);
	lv_obj_set_size(spinner, 30, 30);
	lv_spinner_set_anim_params(spinner, 900, 90);
	lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
	lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
	lv_obj_set_style_arc_rounded(spinner, true, LV_PART_MAIN);
	lv_obj_set_style_arc_rounded(spinner, true, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(spinner, lv_color_hex(0x29313a), LV_PART_MAIN);
	lv_obj_set_style_arc_color(spinner, ui_tone_color(UI_TONE_ACTIVE), LV_PART_INDICATOR);
	lv_obj_center(spinner);
}

static void input_reset(void)
{
	if (ui.input != NULL) {
		(void)lv_async_call_cancel(keyboard_show_deferred, ui.input);
		(void)lv_async_call_cancel(input_submit_deferred, ui.input);
		/* LVGL releases textarea storage without clearing sensitive input. */
		const char *text = lv_textarea_get_text(ui.input);
		if (text != NULL) {
			ui_wipe((void *)text, strlen(text));
		}
		lv_textarea_set_text(ui.input, "");
	}
	if (ui.keyboard != NULL) {
		(void)lv_async_call_cancel(keyboard_hide_deferred, ui.keyboard);
		lv_keyboard_set_textarea(ui.keyboard, NULL);
		lv_obj_delete(ui.keyboard);
	}
	ui.keyboard = NULL;
	ui.input = NULL;
	ui.input_error = NULL;
	ui.input_submit = NULL;
	ui.input_handler = NULL;
}

lv_obj_t *ui_page_begin(const char *title, enum ui_navigation navigation)
{
	ui_dialog_close();
	input_reset();

	lv_obj_clear_flag(ui.status_bar, LV_OBJ_FLAG_HIDDEN);
	ui_status_navigation(navigation);
	lv_obj_clean(ui.content);
	content_bounds();
	lv_obj_set_style_pad_top(ui.content, ui.height <= 240 ? 4 : 8, 0);
	lv_obj_set_style_pad_bottom(ui.content, 10, 0);
	lv_obj_set_style_pad_row(ui.content, 6, 0);
	lv_obj_set_flex_flow(ui.content, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ui.content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);
	lv_obj_set_scrollbar_mode(ui.content, LV_SCROLLBAR_MODE_ACTIVE);

	if (title[0] != '\0') {
		lv_obj_t *label = lv_label_create(ui.content);
		lv_obj_add_style(label, &styles.title, 0);
		lv_obj_set_width(label, LV_PCT(100));
		lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
		lv_label_set_text(label, title);
	}

	return ui.content;
}

lv_obj_t *ui_icon(lv_obj_t *parent, const void *source)
{
	lv_obj_t *image = lv_image_create(parent);
	lv_image_set_src(image, source);
	return image;
}

void ui_icon_color(lv_obj_t *icon, lv_color_t color)
{
	lv_obj_set_style_image_recolor(icon, color, 0);
	lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
}

void ui_press_feedback(lv_obj_t *object)
{
	lv_obj_set_style_bg_color(object, lv_color_hex(0xffffff), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(object, LV_OPA_20, LV_STATE_PRESSED);
	lv_obj_set_style_transform_width(object, 0, LV_STATE_PRESSED);
	lv_obj_set_style_transform_height(object, 0, LV_STATE_PRESSED);
}

void ui_section(lv_obj_t *parent, const char *text)
{
	lv_obj_t *label = lv_label_create(parent);
	lv_obj_add_style(label, &styles.section, 0);
	lv_obj_set_width(label, LV_PCT(100));
	lv_label_set_text(label, text);
}

void ui_list_row(lv_obj_t *parent, const void *icon, const char *title, const char *detail,
		 const char *trailing, enum ui_tone tone, lv_event_cb_t callback, void *user_data)
{
	lv_obj_t *row = callback == NULL ? lv_obj_create(parent) : lv_button_create(parent);
	lv_obj_add_style(row, &styles.list, 0);
	if (callback != NULL) {
		ui_press_feedback(row);
		lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
	} else {
		lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
	}
	lv_obj_set_width(row, LV_PCT(100));
	lv_obj_set_height(row, LV_SIZE_CONTENT);
	int32_t min_height = callback != NULL ? 44 : (detail != NULL ? 40 : 30);
	lv_obj_set_style_min_height(row, min_height, 0);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	if (icon != NULL) {
		ui_icon_color(ui_icon(row, icon), ui_tone_color(tone));
	}

	lv_obj_t *copy = lv_obj_create(row);
	lv_obj_set_style_bg_opa(copy, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(copy, 0, 0);
	lv_obj_set_style_radius(copy, 0, 0);
	lv_obj_set_style_pad_all(copy, 0, 0);
	lv_obj_remove_flag(copy, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					 LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_height(copy, LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(copy, 1);
	lv_obj_set_flex_flow(copy, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(copy, 1, 0);

	lv_obj_t *title_label = lv_label_create(copy);
	lv_obj_add_style(title_label, &styles.text, 0);
	lv_obj_set_width(title_label, LV_PCT(100));
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
	lv_label_set_text(title_label, title);

	if (detail != NULL) {
		lv_obj_t *detail_label = lv_label_create(copy);
		lv_obj_add_style(detail_label, &styles.muted, 0);
		lv_obj_set_width(detail_label, LV_PCT(100));
		lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
		lv_label_set_text(detail_label, detail);
	}

	if (trailing != NULL) {
		lv_obj_t *end = lv_label_create(row);
		lv_obj_add_style(end, &styles.muted, 0);
		lv_obj_set_style_text_color(end, ui_tone_color(tone), 0);
		lv_label_set_text(end, trailing);
	} else if (callback != NULL) {
		ui_icon_color(ui_icon(row, &oskey_chevron_right), ui_tone_color(UI_TONE_MUTED));
	}
}

void ui_error(const char *text)
{
	lv_label_set_text(ui.notice_label, text);
	lv_obj_align(ui.notice, LV_ALIGN_BOTTOM_MID, 0, -10);
	lv_obj_clear_flag(ui.notice, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(ui.notice);
	lv_obj_fade_in(ui.notice, 100, 0);

	if (ui.notice_timer != NULL) {
		lv_timer_delete(ui.notice_timer);
	}
	ui.notice_timer = lv_timer_create(notice_expired, 2600, NULL);
	if (ui.notice_timer != NULL) {
		lv_timer_set_repeat_count(ui.notice_timer, 1);
	}
}

void ui_set_busy(bool active)
{
	if (active) {
		lv_obj_clear_flag(ui.busy, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(ui.busy);
		lv_obj_fade_in(ui.busy, 100, 0);
	} else {
		lv_obj_add_flag(ui.busy, LV_OBJ_FLAG_HIDDEN);
	}
}

void ui_input_error(const char *text)
{
	if (ui.input_error == NULL) {
		ui_error(text);
		return;
	}
	lv_label_set_text(ui.input_error, text);
	lv_obj_clear_flag(ui.input_error, LV_OBJ_FLAG_HIDDEN);
	lv_obj_update_layout(ui.content);
	lv_obj_scroll_to_view_recursive(ui.input_error, LV_ANIM_OFF);
}

static void keyboard_show_deferred(void *input)
{
	if (ui.keyboard == NULL || ui.input != input ||
	    !lv_obj_has_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN)) {
		return;
	}

	lv_keyboard_set_textarea(ui.keyboard, ui.input);
	lv_obj_add_state(ui.input, LV_STATE_FOCUSED);
	lv_obj_add_state(lv_obj_get_parent(ui.input), LV_STATE_FOCUSED);
	if (ui.input_submit != NULL) {
		lv_obj_add_flag(ui.input_submit, LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_clear_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_height(ui.content,
			  ui.height - UI_STATUS_HEIGHT - lv_obj_get_height(ui.keyboard));
	lv_obj_move_foreground(ui.keyboard);
	lv_obj_update_layout(ui.content);
	lv_obj_scroll_to_view_recursive(ui.input, LV_ANIM_OFF);
}

static void keyboard_hide_deferred(void *keyboard)
{
	if (ui.keyboard != keyboard || lv_obj_has_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN)) {
		return;
	}

	lv_obj_add_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_keyboard_set_textarea(ui.keyboard, NULL);
	lv_obj_remove_state(ui.input, LV_STATE_FOCUSED);
	lv_obj_remove_state(lv_obj_get_parent(ui.input), LV_STATE_FOCUSED);
	if (ui.input_submit != NULL) {
		lv_obj_clear_flag(ui.input_submit, LV_OBJ_FLAG_HIDDEN);
	}
	content_bounds();
	lv_obj_scroll_to(ui.content, 0, 0, LV_ANIM_OFF);
}

static void keyboard_show(void)
{
	if (ui.keyboard == NULL || ui.input == NULL) {
		return;
	}

	(void)lv_async_call_cancel(keyboard_hide_deferred, ui.keyboard);
	(void)lv_async_call_cancel(keyboard_show_deferred, ui.input);
	(void)lv_async_call(keyboard_show_deferred, ui.input);
}

static bool keyboard_hide(void)
{
	if (ui.keyboard == NULL || ui.input == NULL) {
		return false;
	}

	bool show_pending = lv_async_call_cancel(keyboard_show_deferred, ui.input) == LV_RESULT_OK;
	if (lv_obj_has_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN)) {
		return show_pending;
	}

	(void)lv_async_call_cancel(keyboard_hide_deferred, ui.keyboard);
	(void)lv_async_call(keyboard_hide_deferred, ui.keyboard);
	return true;
}

static void input_submit_deferred(void *input)
{
	if (ui.input != input || ui.input_handler == NULL) {
		return;
	}

	keyboard_hide();
	ui.input_handler(lv_textarea_get_text(ui.input));
}

static void input_submit(void)
{
	if (ui.input == NULL || ui.input_handler == NULL) {
		return;
	}

	(void)lv_async_call_cancel(input_submit_deferred, ui.input);
	(void)lv_async_call(input_submit_deferred, ui.input);
}

static void input_changed(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (ui.input_error != NULL) {
		lv_obj_add_flag(ui.input_error, LV_OBJ_FLAG_HIDDEN);
	}
}

static void input_action_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	input_submit();
}

static void keyboard_event(lv_event_t *event)
{
	switch (lv_event_get_code(event)) {
	case LV_EVENT_READY:
		input_submit();
		break;
	case LV_EVENT_CANCEL:
		keyboard_hide();
		break;
	default:
		break;
	}
}

static void input_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	keyboard_show();
}

static void password_toggled(lv_event_t *event)
{
	bool hidden = lv_textarea_get_password_mode(ui.input);
	lv_textarea_set_password_mode(ui.input, !hidden);
	lv_image_set_src(lv_event_get_user_data(event), hidden ? &oskey_eye_off : &oskey_eye);
}

void ui_input_page(const struct ui_input_config *config, ui_input_submit_t submit)
{
	lv_obj_t *content = ui_page_begin(
		config->title, ui.page == UI_PAGE_LOCKED ? UI_NAVIGATION_NONE : UI_NAVIGATION_BACK);
	lv_obj_t *description = lv_label_create(content);
	lv_obj_set_width(description, LV_PCT(100));
	lv_obj_set_style_text_color(description, lv_color_hex(0x929eaa), 0);
	lv_obj_set_style_text_font(description, UI_FONT_BODY, 0);
	lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
	lv_label_set_text(description, config->hint);

	lv_obj_t *form = lv_obj_create(content);
	lv_obj_set_size(form, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(form, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(form, 0, 0);
	lv_obj_set_style_radius(form, 0, 0);
	lv_obj_set_style_pad_all(form, 0, 0);
	lv_obj_set_style_pad_row(form, 4, 0);
	lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
	lv_obj_remove_flag(form, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					 LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *field = lv_obj_create(form);
	lv_obj_set_size(field, LV_PCT(100), config->password ? 40 : LV_MIN(88, ui.height / 4));
	lv_obj_set_style_bg_opa(field, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(field, lv_color_hex(0x484848), 0);
	lv_obj_set_style_border_width(field, 1, 0);
	lv_obj_set_style_border_color(field, lv_color_hex(0x4da3ff), LV_STATE_FOCUSED);
	lv_obj_set_style_radius(field, 3, 0);
	lv_obj_set_style_pad_all(field, 0, 0);
	lv_obj_set_style_pad_column(field, 0, 0);
	lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);

	ui.input = lv_textarea_create(field);
	lv_obj_set_height(ui.input, LV_PCT(100));
	lv_obj_set_width(ui.input, config->password ? 0 : LV_PCT(100));
	if (config->password) {
		lv_obj_set_flex_grow(ui.input, 1);
	}
	lv_textarea_set_one_line(ui.input, config->password);
	lv_textarea_set_password_mode(ui.input, config->password);
	lv_textarea_set_max_length(ui.input, config->max_length);
	lv_textarea_set_placeholder_text(ui.input, config->placeholder);
	lv_obj_set_style_text_font(ui.input, UI_FONT_BODY, 0);
	lv_obj_set_style_text_color(ui.input, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_bg_color(ui.input, lv_color_hex(0x4da3ff),
				  LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(ui.input, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(ui.input, lv_color_hex(0xf2f5f7),
				    LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(ui.input, lv_color_hex(0x727e89), LV_PART_TEXTAREA_PLACEHOLDER);
	lv_obj_set_style_text_font(ui.input, UI_FONT_BODY, LV_PART_TEXTAREA_PLACEHOLDER);
	lv_obj_set_style_bg_opa(ui.input, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(ui.input, 0, 0);
	lv_obj_set_style_radius(ui.input, 0, 0);
	lv_obj_set_style_pad_all(ui.input, 6, 0);
	lv_obj_add_event_cb(ui.input, input_changed, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(ui.input, input_clicked, LV_EVENT_CLICKED, NULL);

	if (config->password) {
		lv_obj_t *button = lv_button_create(field);
		lv_obj_set_size(button, 44, 38);
		lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
		ui_press_feedback(button);
		lv_obj_set_style_border_width(button, 0, 0);
		lv_obj_set_style_shadow_width(button, 0, 0);
		lv_obj_set_ext_click_area(button, 3);
		lv_obj_remove_flag(button,
				   LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
		lv_obj_t *eye = ui_icon(button, &oskey_eye);
		ui_icon_color(eye, ui_tone_color(UI_TONE_MUTED));
		lv_obj_center(eye);
		lv_obj_add_event_cb(button, password_toggled, LV_EVENT_CLICKED, eye);
	}

	ui.input_error = lv_label_create(form);
	lv_obj_set_width(ui.input_error, LV_PCT(100));
	lv_obj_set_style_text_color(ui.input_error, lv_color_hex(0xe36a78), 0);
	lv_obj_set_style_text_font(ui.input_error, UI_FONT_CAPTION, 0);
	lv_label_set_long_mode(ui.input_error, LV_LABEL_LONG_WRAP);
	lv_obj_add_flag(ui.input_error, LV_OBJ_FLAG_HIDDEN);

	ui_list_row(content, &oskey_success, config->action, config->action_detail, NULL,
		    UI_TONE_SUCCESS, input_action_clicked, NULL);
	ui.input_submit = lv_obj_get_child(content, -1);
	lv_obj_set_style_margin_top(ui.input_submit, 18, 0);
#if defined(CONFIG_OSKEY_NXP_SE)
	if (ui.page == UI_PAGE_LOCKED) {
		ui_nxp_entry(content);
	}
#endif

	ui.keyboard = lv_keyboard_create(ui.screen);
	lv_obj_set_size(ui.keyboard, LV_PCT(100), LV_MIN(ui.height * 55 / 100, 200));
	lv_obj_align(ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_bg_opa(ui.keyboard, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(ui.keyboard, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(ui.keyboard, lv_palette_lighten(LV_PALETTE_GREY, 2),
				  LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(ui.keyboard, LV_OPA_COVER, LV_PART_ITEMS);
	lv_obj_set_style_text_color(ui.keyboard, lv_palette_darken(LV_PALETTE_GREY, 4),
				    LV_PART_ITEMS);
	lv_obj_set_style_border_color(ui.keyboard, lv_color_hex(0x303944), LV_PART_ITEMS);
	lv_obj_set_style_border_width(ui.keyboard, 1, LV_PART_ITEMS);
	lv_obj_set_style_shadow_width(ui.keyboard, 0, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(ui.keyboard, lv_palette_lighten(LV_PALETTE_GREY, 2),
				  LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_text_color(ui.keyboard, lv_palette_darken(LV_PALETTE_GREY, 4),
				    LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_text_font(ui.keyboard, UI_FONT_BODY, LV_PART_ITEMS);
	lv_obj_add_event_cb(ui.keyboard, keyboard_event, LV_EVENT_ALL, NULL);
	lv_obj_add_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
	ui.input_handler = submit;
}

int ui_submit(enum LocalRequestKind kind, uint32_t value, const void *data, size_t len,
	      const void *auxiliary, size_t auxiliary_len)
{
#if defined(CONFIG_OSKEY_RUST)
	int ret =
		app_core_submit_local(kind, value, data, len, auxiliary, auxiliary_len, K_NO_WAIT);

	if (ret < 0) {
		ui_error(ret == -ENOTSUP ? "Wallet unavailable" : "Device busy");
		return ret;
	}
	ui_set_busy(true);
	return 0;
#else
	ARG_UNUSED(kind);
	ARG_UNUSED(value);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(auxiliary);
	ARG_UNUSED(auxiliary_len);
	return -ENOTSUP;
#endif
}

void ui_refresh(void)
{
	int32_t scroll_y = lv_obj_get_scroll_y(ui.content);

	ui_render();
	lv_obj_scroll_to(ui.content, 0, scroll_y, LV_ANIM_OFF);
}

static void ui_page_leave(enum ui_page page)
{
#if defined(CONFIG_OSKEY_REMOVABLE_MEDIA)
	if (page == UI_PAGE_FILES) {
		ui_files_leave();
	}
#endif
	if (page == UI_PAGE_ENTROPY_COLLECT) {
		ui_entropy_collect_leave();
	}
#if defined(CONFIG_OSKEY_QR_SCANNER)
	if (page == UI_PAGE_QR_SCANNER) {
		ui_qr_leave();
	}
#else
	ARG_UNUSED(page);
#endif
}

void ui_replace(enum ui_page page)
{
	if (ui.page == page) {
		return;
	}
	ui_page_leave(ui.page);
	ui.page = page;
	ui_render();
}

void ui_open(enum ui_page page)
{
	if (ui.page != page) {
		ui_page_leave(ui.page);
	}
	if (page == UI_PAGE_LOCKED || page == UI_PAGE_STORAGE_ERROR) {
		ui_clear_sensitive();
	}
	ui.history_len = 0;
	ui.page = page;
	ui_render();
}

void ui_push(enum ui_page page)
{
	if (ui.page == page) {
		return;
	}
	if (ui.history_len < ARRAY_SIZE(ui.history) && ui.page != UI_PAGE_NONE) {
		ui.history[ui.history_len++] = ui.page;
	}
	ui_page_leave(ui.page);
	ui.page = page;
	ui_render();
}

void ui_back(void)
{
	if (keyboard_hide()) {
		return;
	}
	if (ui.history_len == 0) {
		return;
	}
	if (ui.page == UI_PAGE_MNEMONIC) {
		ui_wipe(ui.mnemonic, sizeof(ui.mnemonic));
	} else if (ui.page == UI_PAGE_PASSPHRASE || ui.page == UI_PAGE_PASSPHRASE_CONFIRM) {
		ui_wipe(ui.passphrase, sizeof(ui.passphrase));
	}
	ui_page_leave(ui.page);
	ui.page = ui.history[--ui.history_len];
	ui_render();
}

void ui_clear_sensitive(void)
{
	ui_wipe(ui.pin, sizeof(ui.pin));
	ui_wipe(ui.mnemonic, sizeof(ui.mnemonic));
	ui_wipe(ui.passphrase, sizeof(ui.passphrase));
	ui_wipe(ui.entropy, sizeof(ui.entropy));
	ui.entropy_bits = 0;
	ui.entropy_session = 0;
	ui.entropy_sources = 0;
	ui.mnemonic_words = 0;
}

void ui_wipe(void *buffer, size_t len)
{
	explicit_bzero(buffer, len);
}
