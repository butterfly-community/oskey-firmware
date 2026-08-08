#ifndef OSKEY_UI_H
#define OSKEY_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl.h>

#include "bindings.h"
#include "app.h"
#include "bus.h"

#define UI_PIN_SIZE         64
#define UI_MNEMONIC_SIZE    256
#define UI_PASSPHRASE_SIZE  249
#define UI_NAVIGATION_DEPTH 8
#define UI_STATUS_HEIGHT    36

#define UI_FONT_CAPTION (&lv_font_montserrat_14)
#define UI_FONT_BODY    (&lv_font_montserrat_16)
#define UI_FONT_TITLE   (&lv_font_montserrat_18)
#define UI_FONT_LARGE   (&lv_font_montserrat_20)

typedef void (*ui_dialog_action_t)(void);
typedef void (*ui_input_submit_t)(const char *text);

struct ui_input_config {
	const char *title;
	const char *hint;
	const char *placeholder;
	const char *action;
	const char *action_detail;
	size_t max_length;
	bool password;
};

enum ui_page {
	UI_PAGE_NONE,
	UI_PAGE_SPLASH,
	UI_PAGE_CAPABILITIES,
	UI_PAGE_LOCKED,
	UI_PAGE_HOME,
	UI_PAGE_CAMERA,
#if defined(CONFIG_OSKEY_QR_SCANNER)
	UI_PAGE_QR_SCANNER,
#endif
	UI_PAGE_SETTINGS,
	UI_PAGE_WIFI,
	UI_PAGE_WIFI_PASSWORD,
	UI_PAGE_BLUETOOTH,
	UI_PAGE_USB,
	UI_PAGE_AUDIO,
	UI_PAGE_PIN_NEW,
	UI_PAGE_PIN_CONFIRM,
	UI_PAGE_FIDO_PIN_RECOVER,
	UI_PAGE_SOURCE,
	UI_PAGE_LENGTH,
	UI_PAGE_IMPORT,
	UI_PAGE_MNEMONIC,
	UI_PAGE_VERIFY,
	UI_PAGE_PASSPHRASE,
	UI_PAGE_PASSPHRASE_CONFIRM,
	UI_PAGE_ENTROPY,
	UI_PAGE_STORAGE_ERROR,
	UI_PAGE_CONFIRMATION,
};

enum ui_tone {
	UI_TONE_DEFAULT,
	UI_TONE_MUTED,
	UI_TONE_ACTIVE,
	UI_TONE_SUCCESS,
	UI_TONE_WARNING,
	UI_TONE_DANGER,
};

enum ui_navigation {
	UI_NAVIGATION_NONE,
	UI_NAVIGATION_BACK,
};

struct ui_status {
	struct app_wifi_state wifi;
	struct app_public_ip public_ip;
	enum app_bluetooth_state bluetooth;
	enum app_usb_state usb;
	enum app_storage_state storage;
	enum app_camera_state camera;
	struct app_audio_status audio;
	enum WalletState wallet;
};

struct ui_context {
	struct ui_status status;
	struct app_wifi_config wifi_config;
	struct app_wifi_scan wifi_scan;
	uint8_t features[APP_FEATURE_COUNT];
	enum ui_page page;
	enum ui_page history[UI_NAVIGATION_DEPTH];
	uint32_t confirmation_id;
	uint8_t history_len;
	bool custom_entropy;
	char pin[UI_PIN_SIZE];
	char mnemonic[UI_MNEMONIC_SIZE];
	char passphrase[UI_PASSPHRASE_SIZE];
	char wifi_ssid[APP_WIFI_SSID_MAX_LEN + 1];
	enum app_wifi_security wifi_security;
	uint8_t entropy[32];
	uint16_t entropy_bits;
	lv_obj_t *screen;
	lv_obj_t *status_bar;
	lv_obj_t *navigation;
	lv_obj_t *wifi_sta_icon;
	lv_obj_t *wifi_ap_icon;
	lv_obj_t *bluetooth_icon;
	lv_obj_t *usb_icon;
	lv_obj_t *camera_icon;
	lv_obj_t *audio_icon;
	lv_obj_t *content;
	lv_obj_t *notice;
	lv_obj_t *notice_label;
	lv_obj_t *busy;
	lv_obj_t *keyboard;
	lv_obj_t *input;
	lv_obj_t *input_error;
	lv_obj_t *input_submit;
	ui_input_submit_t input_handler;
	lv_timer_t *notice_timer;
	int32_t width;
	int32_t height;
};

extern struct ui_context ui;

void ui_init(const uint8_t features[APP_FEATURE_COUNT], const struct ui_status *status,
	     const struct app_wifi_config *wifi_config, const struct app_wifi_scan *wifi_scan);
void ui_controller_init(const uint8_t features[APP_FEATURE_COUNT]);
void ui_show_startup(void);
void ui_open(enum ui_page page);
void ui_push(enum ui_page page);
void ui_back(void);
void ui_render(void);
void ui_refresh(void);
void ui_wipe(void *buffer, size_t len);
void ui_clear_sensitive(void);

lv_obj_t *ui_page_begin(const char *title, enum ui_navigation navigation);
lv_obj_t *ui_icon(lv_obj_t *parent, const void *source);
void ui_icon_color(lv_obj_t *icon, lv_color_t color);
lv_color_t ui_tone_color(enum ui_tone tone);
void ui_press_feedback(lv_obj_t *object);
void ui_section(lv_obj_t *parent, const char *text);
void ui_list_row(lv_obj_t *parent, const void *icon, const char *title, const char *detail,
		 const char *trailing, enum ui_tone tone, lv_event_cb_t callback, void *user_data);
void ui_error(const char *text);
void ui_set_busy(bool active);
void ui_input_error(const char *text);
void ui_input_page(const struct ui_input_config *config, ui_input_submit_t submit);
void ui_dialog_show(const void *icon, const char *title, const char *message, const char *confirm,
		    enum ui_tone tone, ui_dialog_action_t action);
void ui_dialog_close(void);
void ui_submit(enum LocalRequestKind kind, uint32_t value, const void *data, size_t len,
	       const void *auxiliary, size_t auxiliary_len);

void ui_show_confirmation(uint32_t id);
bool ui_render_confirmation(void);
void ui_dismiss_confirmation(void);
void ui_status_init(const struct ui_status *status);
void ui_status_navigation(enum ui_navigation navigation);
void ui_status_update(const struct ui_status *status);

void ui_wifi_render(void);
void ui_wifi_password_render(void);
void ui_wifi_password_submit(const char *password);
void ui_bluetooth_render(void);
void ui_usb_render(void);

enum ui_tone ui_camera_tone(enum app_camera_state state);
void ui_camera_render(void);

#if defined(CONFIG_OSKEY_QR_SCANNER)
struct app_qr_scanner_event;
void ui_qr_render(void);
void ui_qr_leave(void);
void ui_qr_event(const struct app_qr_scanner_event *event);
#endif

#endif
