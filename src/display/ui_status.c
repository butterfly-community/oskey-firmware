#include "ui.h"

#include "assets/assets.h"

static enum ui_tone wifi_tone(enum app_display_wifi_state state)
{
	switch (state) {
	case APP_DISPLAY_WIFI_AP:
		return UI_TONE_ACTIVE;
	case APP_DISPLAY_WIFI_CONNECTING:
		return UI_TONE_WARNING;
	case APP_DISPLAY_WIFI_CONNECTED:
		return UI_TONE_SUCCESS;
	case APP_DISPLAY_WIFI_DISCONNECTED:
	case APP_DISPLAY_WIFI_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

static enum ui_tone bluetooth_tone(enum app_display_bluetooth_state state)
{
	switch (state) {
	case APP_DISPLAY_BLUETOOTH_ADVERTISING:
		return UI_TONE_ACTIVE;
	case APP_DISPLAY_BLUETOOTH_CONNECTED:
		return UI_TONE_SUCCESS;
	case APP_DISPLAY_BLUETOOTH_IDLE:
	case APP_DISPLAY_BLUETOOTH_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

static enum ui_tone usb_tone(enum app_display_usb_state state)
{
	switch (state) {
	case APP_DISPLAY_USB_ATTACHED:
		return UI_TONE_ACTIVE;
	case APP_DISPLAY_USB_CONFIGURED:
		return UI_TONE_SUCCESS;
	case APP_DISPLAY_USB_SUSPENDED:
		return UI_TONE_WARNING;
	case APP_DISPLAY_USB_DISCONNECTED:
	case APP_DISPLAY_USB_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

static void navigation_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_back();
}

void ui_status_init(const struct app_display_status *status)
{
	ui.status_bar = lv_obj_create(ui.screen);
	lv_obj_set_size(ui.status_bar, LV_PCT(100), UI_STATUS_HEIGHT);
	lv_obj_align(ui.status_bar, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_style_bg_opa(ui.status_bar, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(ui.status_bar, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(ui.status_bar, lv_color_hex(0x1c2229), 0);
	lv_obj_set_style_border_width(ui.status_bar, 1, 0);
	lv_obj_set_style_pad_all(ui.status_bar, 0, 0);
	lv_obj_remove_flag(ui.status_bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
						  LV_OBJ_FLAG_SCROLLABLE);

	ui.navigation = lv_button_create(ui.status_bar);
	lv_obj_set_size(ui.navigation, 40, UI_STATUS_HEIGHT - 1);
	lv_obj_align(ui.navigation, LV_ALIGN_LEFT_MID, 0, 0);
	lv_obj_set_style_bg_opa(ui.navigation, LV_OPA_TRANSP, 0);
	lv_obj_set_style_bg_color(ui.navigation, lv_color_hex(0xffffff), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(ui.navigation, LV_OPA_10, LV_STATE_PRESSED);
	lv_obj_set_style_border_width(ui.navigation, 0, 0);
	lv_obj_set_style_shadow_width(ui.navigation, 0, 0);
	lv_obj_set_ext_click_area(ui.navigation, 4);
	lv_obj_add_event_cb(ui.navigation, navigation_clicked, LV_EVENT_CLICKED, NULL);

	lv_obj_center(ui_icon(ui.navigation, &oskey_back));

	lv_obj_t *status_icons = lv_obj_create(ui.status_bar);
	lv_obj_set_size(status_icons, LV_SIZE_CONTENT, UI_STATUS_HEIGHT - 1);
	lv_obj_align(status_icons, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_style_bg_opa(status_icons, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(status_icons, 0, 0);
	lv_obj_set_style_pad_hor(status_icons, 10, 0);
	lv_obj_set_style_pad_column(status_icons, 7, 0);
	lv_obj_set_flex_flow(status_icons, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(status_icons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_remove_flag(status_icons, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
						 LV_OBJ_FLAG_SCROLLABLE);

	ui.wifi_icon = ui_icon(status_icons, &oskey_wifi);
	ui.bluetooth_icon = ui_icon(status_icons, &oskey_bluetooth);
	ui.usb_icon = ui_icon(status_icons, &oskey_usb);
	lv_obj_set_size(ui.wifi_icon, 18, 18);
	lv_obj_set_size(ui.bluetooth_icon, 18, 18);
	lv_obj_set_size(ui.usb_icon, 18, 18);
	lv_image_set_inner_align(ui.wifi_icon, LV_IMAGE_ALIGN_CONTAIN);
	lv_image_set_inner_align(ui.bluetooth_icon, LV_IMAGE_ALIGN_CONTAIN);
	lv_image_set_inner_align(ui.usb_icon, LV_IMAGE_ALIGN_CONTAIN);

	ui_status_navigation(UI_NAVIGATION_NONE);
	ui_status_update(status);
}

void ui_status_navigation(enum ui_navigation navigation)
{
	if (navigation == UI_NAVIGATION_NONE) {
		lv_obj_add_flag(ui.navigation, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(ui.navigation, LV_OBJ_FLAG_HIDDEN);
}

void ui_status_update(const struct app_display_status *status)
{
	ui_icon_color(ui.wifi_icon, ui_tone_color(wifi_tone(status->wifi)));
	ui_icon_color(ui.bluetooth_icon, ui_tone_color(bluetooth_tone(status->bluetooth)));
	ui_icon_color(ui.usb_icon, ui_tone_color(usb_tone(status->usb)));
}
