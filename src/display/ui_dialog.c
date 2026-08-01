#include "ui.h"

#include <stdint.h>

static lv_obj_t *dialog;
static ui_dialog_action_t dialog_action;

static void dialog_clicked(lv_event_t *event)
{
	bool confirmed = (uintptr_t)lv_event_get_user_data(event);
	ui_dialog_action_t action = confirmed ? dialog_action : NULL;

	ui_dialog_close();
	if (action != NULL) {
		action();
	}
}

static void style_button(lv_obj_t *button, lv_color_t color)
{
	lv_obj_set_height(button, 44);
	lv_obj_set_flex_grow(button, 1);
	lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(button, 0, 0);
	lv_obj_set_style_radius(button, 0, 0);
	lv_obj_set_style_shadow_width(button, 0, 0);
	lv_obj_set_style_text_color(button, color, 0);
	lv_obj_set_style_text_font(button, &lv_font_montserrat_12, 0);
	lv_obj_set_style_bg_color(button, lv_color_hex(0xffffff), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(button, LV_OPA_10, LV_STATE_PRESSED);
}

static bool dialog_begin(const void *icon, const char *title, const char *message)
{
	ui_dialog_close();
	dialog = lv_msgbox_create(NULL);
	if (dialog == NULL) {
		ui_error("Unable to open dialog");
		return false;
	}

	lv_obj_t *backdrop = lv_obj_get_parent(dialog);
	lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
	lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
	lv_obj_set_style_border_width(backdrop, 0, 0);

	lv_obj_set_width(dialog, LV_MIN(ui.width - 24, 420));
	lv_obj_set_height(dialog, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_color(dialog, lv_color_hex(0x11161c), 0);
	lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(dialog, lv_color_hex(0x343d46), 0);
	lv_obj_set_style_border_width(dialog, 1, 0);
	lv_obj_set_style_radius(dialog, 8, 0);
	lv_obj_set_style_shadow_width(dialog, 0, 0);
	lv_obj_set_style_pad_all(dialog, 0, 0);

	lv_obj_t *title_label = lv_msgbox_add_title(dialog, title);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);

	lv_obj_t *header = lv_msgbox_get_header(dialog);
	lv_obj_set_height(header, 36);
	lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(header, lv_color_hex(0x29313a), 0);
	lv_obj_set_style_border_width(header, 1, 0);
	lv_obj_set_style_pad_hor(header, 14, 0);

	lv_obj_t *content = lv_msgbox_get_content(dialog);
	lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(content, 0, 0);
	lv_obj_set_style_pad_hor(content, 14, 0);
	lv_obj_set_style_pad_ver(content, 8, 0);
	lv_obj_set_style_pad_row(content, 7, 0);
	lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	if (icon != NULL) {
		ui_icon(content, icon);
	}
	lv_obj_t *message_label = lv_msgbox_add_text(dialog, message);
	lv_obj_set_style_text_color(message_label, lv_color_hex(0xb8c1ca), 0);
	lv_obj_set_style_text_font(message_label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
	return true;
}

static void dialog_finish(void)
{
	lv_obj_t *footer = lv_msgbox_get_footer(dialog);
	lv_obj_set_width(footer, LV_PCT(100));
	lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
	lv_obj_set_style_border_color(footer, lv_color_hex(0x29313a), 0);
	lv_obj_set_style_border_width(footer, 1, 0);
	lv_obj_set_style_pad_all(footer, 0, 0);
	lv_obj_set_style_pad_column(footer, 0, 0);

	lv_obj_center(dialog);
	lv_obj_fade_in(lv_obj_get_parent(dialog), 120, 0);
}

void ui_dialog_show(const void *icon, const char *title, const char *message, const char *confirm,
		    enum ui_tone tone, ui_dialog_action_t action)
{
	if (!dialog_begin(icon, title, message)) {
		return;
	}
	dialog_action = action;

	lv_obj_t *cancel = lv_msgbox_add_footer_button(dialog, "Cancel");
	style_button(cancel, lv_color_hex(0xb8c1ca));
	lv_obj_add_event_cb(cancel, dialog_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_set_style_border_side(cancel, LV_BORDER_SIDE_RIGHT, 0);
	lv_obj_set_style_border_color(cancel, lv_color_hex(0x29313a), 0);
	lv_obj_set_style_border_width(cancel, 1, 0);

	lv_color_t color = ui_tone_color(tone == UI_TONE_DEFAULT ? UI_TONE_ACTIVE : tone);
	lv_obj_t *accept = lv_msgbox_add_footer_button(dialog, confirm);
	style_button(accept, color);
	lv_obj_add_event_cb(accept, dialog_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)true);
	dialog_finish();
}

void ui_dialog_close(void)
{
	if (dialog != NULL) {
		lv_msgbox_close(dialog);
		dialog = NULL;
	}
	dialog_action = NULL;
}
