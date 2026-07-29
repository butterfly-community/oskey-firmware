#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include <stddef.h>
#include <stdint.h>

#include "bindings.h"

#ifdef CONFIG_OSKEY_DISPLAY

#include <lvgl.h>
#include <zephyr/kernel.h>

typedef enum {
	INIT_ACTION_GENERATE = 1,
	INIT_ACTION_IMPORT = 2
} app_init_action_t;

typedef enum {
	TOOLS_ACTION_ERASE_DATA = 1,
	TOOLS_ACTION_RESTART = 2,
	TOOLS_ACTION_PIN_SETTING = 3
} app_tools_action_t;

typedef enum {
	INPUT_ACTION_IMPORT = 0,
	INPUT_ACTION_CHECK_MNEMONIC = 2,
	INPUT_ACTION_PIN_SET = 300,
	INPUT_ACTION_PIN_CONFIRM = 301,
	INPUT_ACTION_PIN_VERIFY = 310
} app_input_action_t;

typedef enum {
	BACK_ACTION_NONE = 0,
	BACK_ACTION_TO_INIT = 1,
	BACK_ACTION_TO_INDEX = 2,
	BACK_ACTION_TO_SELECT_LENGTH = 3,
	BACK_ACTION_TO_TOOLS = 4,
	BACK_ACTION_TO_CHECK_FEATURES = 5,
	BACK_ACTION_REJECT = 6
} app_back_action_t;

typedef enum {
	MNEMONIC_LENGTH_12 = 12,
	MNEMONIC_LENGTH_18 = 18,
	MNEMONIC_LENGTH_24 = 24
} app_mnemonic_length_t;

void hide_error_label(lv_timer_t *timer);
void app_display_init_cb(lv_event_t *e);
void app_display_tools_cb(lv_event_t *e);
void app_display_mnemonic_cb();
void app_display_index_cb(lv_event_t *e);

void app_display_mnemonic_process(void *param);

void back_button_event_handler(lv_event_t *e);

void app_display_init_show_select_length(void);
void app_display_input(char *title_text, uintptr_t action, uintptr_t back_action);
void app_display_tools(void);
void app_display_logo(void);
void app_display_features(void);
void app_display_entropy_collection(int page_count);

#endif /* CONFIG_OSKEY_DISPLAY */

int app_init_display(void);
void app_display_loop(void);
void app_display_message(AppDisplayAction action, const uint8_t *data, size_t len);

#endif /* APP_SCREEN_H */
