#include "ui.h"

#include <errno.h>
#include <string.h>
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
	lv_style_t list_pressed;
} styles;

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
	lv_style_set_text_font(&styles.screen, &lv_font_montserrat_14);
	lv_style_set_border_width(&styles.screen, 0);
	lv_style_set_pad_all(&styles.screen, 0);

	lv_style_init(&styles.content);
	lv_style_set_bg_opa(&styles.content, LV_OPA_TRANSP);
	lv_style_set_text_color(&styles.content, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.content, &lv_font_montserrat_12);
	lv_style_set_border_width(&styles.content, 0);
	lv_style_set_radius(&styles.content, 0);
	lv_style_set_pad_row(&styles.content, 8);

	lv_style_init(&styles.title);
	lv_style_set_text_color(&styles.title, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.title, &lv_font_montserrat_14);
	lv_style_set_pad_top(&styles.title, 2);
	lv_style_set_pad_bottom(&styles.title, 4);

	lv_style_init(&styles.text);
	lv_style_set_text_color(&styles.text, lv_color_hex(0xf2f5f7));
	lv_style_set_text_font(&styles.text, &lv_font_montserrat_12);

	lv_style_init(&styles.muted);
	lv_style_set_text_color(&styles.muted, lv_color_hex(0x929eaa));
	lv_style_set_text_font(&styles.muted, &lv_font_montserrat_10);

	lv_style_init(&styles.section);
	lv_style_set_text_color(&styles.section, lv_color_hex(0x929eaa));
	lv_style_set_text_font(&styles.section, &lv_font_montserrat_10);
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

	lv_style_init(&styles.list_pressed);
	lv_style_set_bg_color(&styles.list_pressed, lv_color_hex(0xffffff));
	lv_style_set_bg_opa(&styles.list_pressed, LV_OPA_10);
}

static void content_bounds(void)
{
	lv_obj_set_size(ui.content, LV_PCT(100), ui.height - UI_STATUS_HEIGHT);
	lv_obj_align(ui.content, LV_ALIGN_TOP_MID, 0, UI_STATUS_HEIGHT);
}

void ui_init(const uint8_t features[APP_FEATURE_COUNT], const struct app_display_status *status)
{
	memset(&ui, 0, sizeof(ui));
	memcpy(ui.features, features, sizeof(ui.features));
	ui.status = *status;
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
	lv_obj_set_style_bg_color(ui.notice, lv_color_hex(0x1a222a), LV_STATE_PRESSED);
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
	lv_obj_set_style_text_font(ui.notice_label, &lv_font_montserrat_12, 0);
	lv_label_set_long_mode(ui.notice_label, LV_LABEL_LONG_WRAP);
	lv_obj_add_flag(ui.notice, LV_OBJ_FLAG_HIDDEN);

	ui.busy = lv_obj_create(ui.screen);
	lv_obj_set_size(ui.busy, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(ui.busy, lv_color_hex(0x000000), 0);
	lv_obj_set_style_bg_opa(ui.busy, LV_OPA_70, 0);
	lv_obj_set_style_border_width(ui.busy, 0, 0);
	lv_obj_set_style_radius(ui.busy, 0, 0);
	lv_obj_set_style_pad_all(ui.busy, 0, 0);
	lv_obj_set_flex_flow(ui.busy, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ui.busy, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_add_flag(ui.busy, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(ui.busy, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(ui.busy, LV_OBJ_FLAG_HIDDEN);

	lv_obj_t *panel = lv_obj_create(ui.busy);
	lv_obj_set_size(panel, LV_MIN(ui.width - 48, 220), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_color(panel, lv_color_hex(0x11161c), 0);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(0x343d46), 0);
	lv_obj_set_style_border_width(panel, 1, 0);
	lv_obj_set_style_radius(panel, 8, 0);
	lv_obj_set_style_pad_all(panel, 14, 0);
	lv_obj_set_style_pad_row(panel, 10, 0);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					  LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *spinner = lv_spinner_create(panel);
	lv_obj_set_size(spinner, 34, 34);
	lv_spinner_set_anim_params(spinner, 800, 260);
	lv_obj_set_style_arc_color(spinner, lv_color_hex(0x4da3ff), LV_PART_INDICATOR);

	lv_obj_t *busy_label = lv_label_create(panel);
	lv_obj_add_style(busy_label, &styles.text, 0);
	lv_obj_set_width(busy_label, LV_PCT(100));
	lv_obj_set_style_text_align(busy_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(busy_label, LV_LABEL_LONG_WRAP);
	lv_label_set_text(busy_label, "Working...");
}

lv_obj_t *ui_page_begin(const char *title, enum ui_navigation navigation)
{
	ui_dialog_close();
	if (ui.input != NULL) {
		/* LVGL releases textarea storage without clearing sensitive input. */
		const char *text = lv_textarea_get_text(ui.input);
		if (text != NULL) {
			ui_wipe((void *)text, strlen(text));
		}
		lv_textarea_set_text(ui.input, "");
	}
	if (ui.keyboard != NULL) {
		lv_keyboard_set_textarea(ui.keyboard, NULL);
		lv_obj_delete(ui.keyboard);
	}
	ui.keyboard = NULL;
	ui.input = NULL;
	ui.input_error = NULL;

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
		lv_obj_add_style(row, &styles.list_pressed, LV_STATE_PRESSED);
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

void ui_keyboard_show(void)
{
	if (ui.keyboard == NULL || ui.input == NULL) {
		return;
	}

	lv_keyboard_set_textarea(ui.keyboard, ui.input);
	lv_obj_add_state(ui.input, LV_STATE_FOCUSED);
	lv_obj_add_state(lv_obj_get_parent(ui.input), LV_STATE_FOCUSED);
	lv_obj_clear_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_height(ui.content,
			  ui.height - UI_STATUS_HEIGHT - lv_obj_get_height(ui.keyboard));
	lv_obj_move_foreground(ui.keyboard);
	lv_obj_update_layout(ui.content);
	lv_obj_scroll_to_view_recursive(ui.input, LV_ANIM_OFF);
}

bool ui_keyboard_hide(void)
{
	if (ui.keyboard == NULL || lv_obj_has_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN)) {
		return false;
	}

	lv_obj_add_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_keyboard_set_textarea(ui.keyboard, NULL);
	lv_obj_remove_state(ui.input, LV_STATE_FOCUSED);
	lv_obj_remove_state(lv_obj_get_parent(ui.input), LV_STATE_FOCUSED);
	content_bounds();
	lv_obj_scroll_to(ui.content, 0, 0, LV_ANIM_OFF);
	return true;
}

void ui_submit(enum LocalRequestKind kind, uint32_t value, const void *data, size_t len,
	       const void *auxiliary, size_t auxiliary_len)
{
	int ret =
		app_core_submit_local(kind, value, data, len, auxiliary, auxiliary_len, K_NO_WAIT);

	if (ret < 0) {
		ui_error(ret == -ENOTSUP ? "Wallet unavailable" : "Device busy");
		return;
	}
	ui_set_busy(true);
}

void ui_open(enum ui_page page)
{
	if (page == UI_PAGE_LOCKED || page == UI_PAGE_STORAGE_ERROR) {
		ui_clear_sensitive();
	}
	ui.history_len = 0;
	ui.page = page;
	ui_render();
}

void ui_push(enum ui_page page)
{
	if (ui.history_len < ARRAY_SIZE(ui.history) && ui.page != UI_PAGE_NONE) {
		ui.history[ui.history_len++] = ui.page;
	}
	ui.page = page;
	ui_render();
}

void ui_back(void)
{
	if (ui_keyboard_hide()) {
		return;
	}
	if (ui.history_len == 0) {
		return;
	}
	if (ui.page == UI_PAGE_MNEMONIC) {
		ui_wipe(ui.mnemonic, sizeof(ui.mnemonic));
	}
	ui.page = ui.history[--ui.history_len];
	ui_render();
}

void ui_clear_sensitive(void)
{
	ui_wipe(ui.pin, sizeof(ui.pin));
	ui_wipe(ui.fido_pin, sizeof(ui.fido_pin));
	ui_wipe(ui.mnemonic, sizeof(ui.mnemonic));
	ui_wipe(ui.entropy, sizeof(ui.entropy));
	ui.entropy_bits = 0;
	ui.custom_entropy = false;
}

void ui_wipe(void *buffer, size_t len)
{
	volatile uint8_t *bytes = buffer;

	while (len-- > 0) {
		*bytes++ = 0;
	}
}
