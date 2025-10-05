#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include "storage.h"
#include "wrapper.h"

#ifdef CONFIG_DISPLAY

#include <lvgl.h>
#include <zephyr/kernel.h>

void app_mnemonic_generate_work_handler(struct k_work *work);
void app_mnemonic_generate_trigger(void);
void app_display_mnemonic_process(void *param);
void app_wallet_init_custom_work_handler(struct k_work *work);
void app_wallet_init_custom_trigger(void);
void back_button_event_handler(lv_event_t *e);

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
	BACK_ACTION_TO_CHECK_FEATURES = 5
} app_back_action_t;

typedef enum {
	MNEMONIC_LENGTH_12 = 12,
	MNEMONIC_LENGTH_18 = 18,
	MNEMONIC_LENGTH_24 = 24
} app_mnemonic_length_t;

typedef struct {
	int action_type;
	int action_value;
} app_event_data_t;

void app_display_init_show_select_length();

void app_display_input(char *title_text, uintptr_t action, uintptr_t back_action);

void app_display_tools();

void app_display_features();

#endif // CONFIG_DISPLAY

int app_init_display();

void app_display_loop();

#endif
