#ifndef OSKEY_UI_H
#define OSKEY_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl.h>

#include "bindings.h"
#include "display.h"

#define UI_PIN_SIZE         30
#define UI_MNEMONIC_SIZE    256
#define UI_NAVIGATION_DEPTH 8
#define UI_STATUS_HEIGHT    36

typedef void (*ui_dialog_action_t)(void);

enum ui_page {
	UI_PAGE_NONE,
	UI_PAGE_SPLASH,
	UI_PAGE_CAPABILITIES,
	UI_PAGE_LOCKED,
	UI_PAGE_HOME,
	UI_PAGE_SETTINGS,
	UI_PAGE_PIN_NEW,
	UI_PAGE_PIN_CONFIRM,
	UI_PAGE_SOURCE,
	UI_PAGE_LENGTH,
	UI_PAGE_IMPORT,
	UI_PAGE_MNEMONIC,
	UI_PAGE_VERIFY,
	UI_PAGE_ENTROPY,
	UI_PAGE_STORAGE_ERROR,
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

struct ui_context {
	struct app_display_startup startup;
	enum ui_page page;
	enum ui_page history[UI_NAVIGATION_DEPTH];
	enum ui_page confirmation_return;
	uint8_t history_len;
	bool custom_entropy;
	char pin[UI_PIN_SIZE];
	char mnemonic[UI_MNEMONIC_SIZE];
	uint8_t entropy[32];
	uint16_t entropy_bits;
	lv_obj_t *screen;
	lv_obj_t *status_bar;
	lv_obj_t *navigation;
	lv_obj_t *wifi_icon;
	lv_obj_t *bluetooth_icon;
	lv_obj_t *usb_icon;
	lv_obj_t *content;
	lv_obj_t *notice;
	lv_obj_t *notice_label;
	lv_obj_t *busy;
	lv_obj_t *keyboard;
	lv_obj_t *input;
	lv_obj_t *input_error;
	lv_timer_t *notice_timer;
	int32_t width;
	int32_t height;
};

extern struct ui_context ui;

void ui_init(const struct app_display_startup *startup);
void ui_show_startup(void);
void ui_open(enum ui_page page);
void ui_push(enum ui_page page);
void ui_back(void);
void ui_render(void);
void ui_wipe(void *buffer, size_t len);
void ui_clear_sensitive(void);

lv_obj_t *ui_page_begin(const char *title, enum ui_navigation navigation);
lv_obj_t *ui_icon(lv_obj_t *parent, const void *source);
void ui_icon_color(lv_obj_t *icon, lv_color_t color);
lv_color_t ui_tone_color(enum ui_tone tone);
void ui_section(lv_obj_t *parent, const char *text);
void ui_list_row(lv_obj_t *parent, const void *icon, const char *title, const char *detail,
		 const char *trailing, enum ui_tone tone, lv_event_cb_t callback, void *user_data);
void ui_error(const char *text);
void ui_set_busy(bool active);
void ui_input_error(const char *text);
void ui_keyboard_show(void);
bool ui_keyboard_hide(void);
void ui_dialog_show(const void *icon, const char *title, const char *message, const char *confirm,
		    enum ui_tone tone, ui_dialog_action_t action);
void ui_dialog_close(void);
void ui_submit(AppMessageAction action, uint32_t value, const void *data, size_t len,
	       const void *auxiliary, size_t auxiliary_len);

void ui_show_confirmation(const struct AppConfirmationView *confirmation);
void ui_dismiss_confirmation(void);
void ui_status_init(const struct app_display_status *status);
void ui_status_navigation(enum ui_navigation navigation);
void ui_status_update(const struct app_display_status *status);

#endif
