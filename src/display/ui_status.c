#include "ui.h"

#include <stdint.h>

#include "assets/assets.h"

enum status_item {
	STATUS_ITEM_WIFI_STA,
	STATUS_ITEM_WIFI_AP,
	STATUS_ITEM_BLUETOOTH,
	STATUS_ITEM_USB,
	STATUS_ITEM_CAMERA,
	STATUS_ITEM_AUDIO,
	STATUS_ITEM_GYRO,
};

static enum ui_tone wifi_sta_tone(enum app_wifi_sta_state state)
{
	if (state == APP_WIFI_STA_CONNECTED) {
		return UI_TONE_SUCCESS;
	}
	if (state == APP_WIFI_STA_CONNECTING) {
		return UI_TONE_WARNING;
	}
	return UI_TONE_MUTED;
}

static bool wifi_ap_visible(enum app_wifi_ap_state state)
{
	return state != APP_WIFI_AP_DISABLED && state != APP_WIFI_AP_OFF;
}

static enum ui_tone bluetooth_tone(enum app_bluetooth_state state)
{
	switch (state) {
	case APP_BLUETOOTH_ADVERTISING:
		return UI_TONE_ACTIVE;
	case APP_BLUETOOTH_CONNECTED:
		return UI_TONE_SUCCESS;
	case APP_BLUETOOTH_IDLE:
	case APP_BLUETOOTH_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

static enum ui_tone usb_tone(enum app_usb_state state)
{
	switch (state) {
	case APP_USB_ATTACHED:
		return UI_TONE_ACTIVE;
	case APP_USB_CONFIGURED:
		return UI_TONE_SUCCESS;
	case APP_USB_SUSPENDED:
		return UI_TONE_WARNING;
	case APP_USB_DISCONNECTED:
	case APP_USB_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

enum ui_tone ui_camera_tone(enum app_camera_state state)
{
	switch (state) {
	case APP_CAMERA_ACTIVE:
		return UI_TONE_SUCCESS;
	case APP_CAMERA_INITIALIZING:
	case APP_CAMERA_STARTING:
		return UI_TONE_WARNING;
	case APP_CAMERA_ERROR:
		return UI_TONE_DANGER;
	case APP_CAMERA_READY:
	case APP_CAMERA_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

#if defined(CONFIG_OSKEY_AUDIO)
static enum ui_tone audio_tone(enum app_audio_state state)
{
	switch (state) {
	case APP_AUDIO_PLAYING:
		return UI_TONE_ACTIVE;
	case APP_AUDIO_IDLE:
		return UI_TONE_SUCCESS;
	case APP_AUDIO_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}
#endif

#if defined(CONFIG_OSKEY_IMU)
static enum ui_tone gyro_tone(enum app_imu_state state)
{
	switch (state) {
	case APP_IMU_READY:
		return UI_TONE_SUCCESS;
	case APP_IMU_INITIALIZING:
		return UI_TONE_WARNING;
	case APP_IMU_IDLE:
		return UI_TONE_MUTED;
	case APP_IMU_ERROR:
		return UI_TONE_DANGER;
	case APP_IMU_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}
#endif

static void status_clicked(lv_event_t *event)
{
	enum status_item item = (enum status_item)(uintptr_t)lv_event_get_user_data(event);
	enum ui_page page;
	if (ui.status.wallet != WalletState_Ready) {
		return;
	}

	switch (item) {
	case STATUS_ITEM_WIFI_STA:
	case STATUS_ITEM_WIFI_AP:
		page = UI_PAGE_WIFI;
		break;
	case STATUS_ITEM_BLUETOOTH:
		page = UI_PAGE_BLUETOOTH;
		break;
	case STATUS_ITEM_USB:
		page = UI_PAGE_USB;
		break;
	case STATUS_ITEM_CAMERA:
		page = UI_PAGE_CAMERA;
		break;
	case STATUS_ITEM_AUDIO:
		page = UI_PAGE_AUDIO;
		break;
	case STATUS_ITEM_GYRO:
		page = UI_PAGE_IMU;
		break;
	default:
		return;
	}
	if (ui.page == page) {
		return;
	}
	bool status_page = ui.page == UI_PAGE_WIFI || ui.page == UI_PAGE_BLUETOOTH ||
			   ui.page == UI_PAGE_USB || ui.page == UI_PAGE_CAMERA ||
			   ui.page == UI_PAGE_AUDIO || ui.page == UI_PAGE_IMU;
#if defined(CONFIG_OSKEY_QR_SCANNER)
	status_page = status_page || ui.page == UI_PAGE_QR_SCANNER;
#endif
	if (status_page) {
		ui_replace(page);
	} else {
		ui_push(page);
	}
}

static lv_obj_t *status_icon(lv_obj_t *parent, const void *source, enum status_item item)
{
	lv_obj_t *button = lv_button_create(parent);
	lv_obj_set_size(button, 28, UI_STATUS_HEIGHT - 1);
	lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
	ui_press_feedback(button);
	lv_obj_set_style_border_width(button, 0, 0);
	lv_obj_set_style_radius(button, 4, 0);
	lv_obj_set_style_shadow_width(button, 0, 0);
	lv_obj_set_style_pad_all(button, 0, 0);
	lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(button, status_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)item);

	lv_obj_t *icon = ui_icon(button, source);
	lv_image_set_scale(icon, LV_SCALE_NONE * 5 / 6);
	lv_obj_center(icon);
	return icon;
}

static void navigation_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_back();
}

void ui_status_init(const struct ui_status *status)
{
	ui.status_bar = lv_obj_create(ui.screen);
	lv_obj_set_size(ui.status_bar, LV_PCT(100), UI_STATUS_HEIGHT);
	lv_obj_align(ui.status_bar, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_style_bg_opa(ui.status_bar, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(ui.status_bar, 0, 0);
	lv_obj_set_style_pad_all(ui.status_bar, 0, 0);
	lv_obj_remove_flag(ui.status_bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
						  LV_OBJ_FLAG_SCROLLABLE);

	ui.navigation = lv_button_create(ui.status_bar);
	lv_obj_set_size(ui.navigation, 40, UI_STATUS_HEIGHT - 1);
	lv_obj_align(ui.navigation, LV_ALIGN_LEFT_MID, 0, 0);
	lv_obj_set_style_bg_opa(ui.navigation, LV_OPA_TRANSP, 0);
	ui_press_feedback(ui.navigation);
	lv_obj_set_style_border_width(ui.navigation, 0, 0);
	lv_obj_set_style_radius(ui.navigation, 4, 0);
	lv_obj_set_style_shadow_width(ui.navigation, 0, 0);
	lv_obj_set_ext_click_area(ui.navigation, 4);
	lv_obj_add_event_cb(ui.navigation, navigation_clicked, LV_EVENT_CLICKED, NULL);

	lv_obj_t *back = ui_icon(ui.navigation, &oskey_back);
	ui_icon_color(back, ui_tone_color(UI_TONE_DEFAULT));
	lv_obj_center(back);

	lv_obj_t *status_icons = lv_obj_create(ui.status_bar);
	lv_obj_set_size(status_icons, LV_SIZE_CONTENT, UI_STATUS_HEIGHT - 1);
	lv_obj_align(status_icons, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_style_bg_opa(status_icons, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(status_icons, 0, 0);
	lv_obj_set_style_pad_hor(status_icons, 10, 0);
	lv_obj_set_style_pad_column(status_icons, 2, 0);
	lv_obj_set_flex_flow(status_icons, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(status_icons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_remove_flag(status_icons, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
						 LV_OBJ_FLAG_SCROLLABLE);

	ui.wifi_sta_icon = status_icon(status_icons, &oskey_wifi, STATUS_ITEM_WIFI_STA);
	ui.wifi_ap_icon = status_icon(status_icons, &oskey_wifi_ap, STATUS_ITEM_WIFI_AP);
	ui.bluetooth_icon = status_icon(status_icons, &oskey_bluetooth, STATUS_ITEM_BLUETOOTH);
	ui.usb_icon = status_icon(status_icons, &oskey_usb, STATUS_ITEM_USB);
	ui.camera_icon = status_icon(status_icons, &oskey_camera, STATUS_ITEM_CAMERA);
#if defined(CONFIG_OSKEY_AUDIO)
	ui.audio_icon = status_icon(status_icons, &oskey_audio, STATUS_ITEM_AUDIO);
#endif
#if defined(CONFIG_OSKEY_IMU)
	ui.gyro_icon = status_icon(status_icons, &oskey_imu, STATUS_ITEM_GYRO);
#endif

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

void ui_status_update(const struct ui_status *status)
{
	ui_icon_color(ui.wifi_sta_icon, ui_tone_color(wifi_sta_tone(status->wifi.sta)));
	if (wifi_ap_visible(status->wifi.ap)) {
		ui_icon_color(ui.wifi_ap_icon, ui_tone_color(UI_TONE_ACTIVE));
		lv_obj_clear_flag(lv_obj_get_parent(ui.wifi_ap_icon), LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(lv_obj_get_parent(ui.wifi_ap_icon), LV_OBJ_FLAG_HIDDEN);
	}
	ui_icon_color(ui.bluetooth_icon, ui_tone_color(bluetooth_tone(status->bluetooth)));
	ui_icon_color(ui.usb_icon, ui_tone_color(usb_tone(status->usb)));
	ui_icon_color(ui.camera_icon, ui_tone_color(ui_camera_tone(status->camera)));
#if defined(CONFIG_OSKEY_AUDIO)
	ui_icon_color(ui.audio_icon, ui_tone_color(audio_tone(status->audio.state)));
#endif
#if defined(CONFIG_OSKEY_IMU)
	ui_icon_color(ui.gyro_icon, ui_tone_color(gyro_tone(status->imu)));
#endif
}
