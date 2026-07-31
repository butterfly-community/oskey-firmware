#include "lvgl.h"

#ifdef CONFIG_OSKEY_DISPLAY

#include <errno.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <lvgl_zephyr.h>
#include "wrapper.h"
#include "storage.h"
#include "app.h"
#include "confirmation.h"
#include "message.h"

#ifdef CONFIG_LV_Z_DEMO_BENCHMARK
#include <lv_demos.h>
#endif

#define DISPLAY_SIGN_TEXT_MAX_LEN 1023

static const struct device *display_dev;
static lv_timer_t *display_start_timer;

static char check_mnemonic_buffer[256] = {0};
static char pin_buffer[30] = {0};

static lv_obj_t *cont = NULL;
static int title_height = 0;
static bool custom_mode = false;
static uint8_t entropy_bytes[32];

static void display_start(lv_timer_t *timer);
static void hide_error_label(lv_timer_t *timer);

static lv_obj_t *create_button_container(lv_obj_t *parent)
{
	lv_obj_t *btn_cont = lv_obj_create(parent);
	lv_obj_set_size(btn_cont, LV_PCT(100), 60);
	lv_obj_set_style_border_width(btn_cont, 0, 0);
	lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(btn_cont, 0, 0);
	lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	return btn_cont;
}

static lv_obj_t *create_centered_button(lv_obj_t *parent, const char *text, lv_color_t bg_color,
					int width, lv_event_cb_t event_cb, void *user_data)
{
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, width, 40);
	lv_obj_set_style_bg_color(btn, bg_color, 0);
	lv_obj_set_style_radius(btn, 8, 0);

	lv_obj_t *label = lv_label_create(btn);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, lv_color_black(), 0);
	lv_obj_center(label);

	if (event_cb) {
		lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, user_data);
	}

	return btn;
}

static void resize_container_for_keyboard(bool show_keyboard)
{
	if (!cont) {
		return;
	}

	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);
	lv_coord_t new_height = show_keyboard ? screen_height - title_height - (screen_height / 2)
					      : screen_height - title_height;
	lv_obj_set_size(cont, lv_disp_get_hor_res(NULL), new_height);
}

static lv_obj_t *create_title_bar(const char *text, lv_color_t border_color)
{
	lv_obj_t *screen = lv_scr_act();
	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, text);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, border_color, 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
	lv_obj_update_layout(title);

	return title;
}

static lv_obj_t *create_back_button(lv_coord_t title_height, app_back_action_t back_action)
{
	lv_obj_t *screen = lv_scr_act();

	lv_obj_t *back_btn = lv_btn_create(screen);
	lv_obj_set_size(back_btn, 40, 40);
	lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (title_height - 40) / 2);
	lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(back_btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_outline_opa(back_btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_shadow_opa(back_btn, LV_OPA_TRANSP, 0);
	lv_obj_move_foreground(back_btn);

	lv_obj_t *back_label = lv_label_create(back_btn);
	lv_label_set_text(back_label, LV_SYMBOL_LEFT);
	lv_obj_set_style_text_color(back_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
	lv_obj_center(back_label);

	lv_obj_add_event_cb(back_btn, back_button_event_handler, LV_EVENT_CLICKED,
			    (void *)back_action);

	return back_btn;
}

static lv_obj_t *create_content_container(lv_coord_t title_height, lv_flex_align_t main_align)
{
	lv_obj_t *screen = lv_scr_act();
	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *container = lv_obj_create(screen);
	lv_obj_set_size(container, screen_width, screen_height - title_height);
	lv_obj_set_pos(container, 0, title_height);
	lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(container, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(container, 0, 0);
	lv_obj_set_style_bg_color(container, lv_color_black(), 0);
	lv_obj_set_style_radius(container, 0, 0);
	lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_pad_row(container, 20, 0);

	return container;
}

static lv_obj_t *create_error_label(void)
{
	lv_obj_t *error_label = lv_label_create(lv_scr_act());
	lv_obj_set_style_text_color(error_label, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_align(error_label, LV_ALIGN_BOTTOM_MID, 0, -20);

	lv_timer_create(hide_error_label, 2000, error_label);

	return error_label;
}

static void show_fail(const char *msg)
{
	lv_label_set_text(create_error_label(), msg ? msg : "Verification failed!");
}

static void display_submit(AppMessageAction action, uint32_t value, const void *data, size_t len,
			   const void *auxiliary, size_t auxiliary_len)
{
	if (!app_message_submit(AppMessageSource_Display, action, value, data, len, auxiliary,
				auxiliary_len)) {
		show_fail("Device busy");
	}
}

static void hide_error_label(lv_timer_t *timer)
{
	lv_obj_t *label = (lv_obj_t *)timer->user_data;
	lv_obj_del(label);
	lv_timer_del(timer);
}

static bool validate_pin_complexity(const char *pin)
{
	if (!pin || strlen(pin) < 8) {
		return false;
	}

	bool has_digit = false, has_lower = false, has_upper = false, has_symbol = false;

	for (int i = 0; pin[i] != '\0'; i++) {
		char c = pin[i];
		if (c >= '0' && c <= '9') {
			has_digit = true;
		} else if (c >= 'a' && c <= 'z') {
			has_lower = true;
		} else if (c >= 'A' && c <= 'Z') {
			has_upper = true;
		} else if (c >= 32 && c <= 126) {
			has_symbol = true;
		}
	}

	return has_digit && has_lower && has_upper && has_symbol;
}

int app_init_display(void)
{
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		return -ENODEV;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	struct gpio_dt_spec lcd_backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);

	if (!gpio_is_ready_dt(&lcd_backlight)) {
		return -ENODEV;
	}

	gpio_pin_configure_dt(&lcd_backlight, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&lcd_backlight, 1);
#endif

	lvgl_lock();
	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);

#ifdef CONFIG_LV_Z_DEMO_BENCHMARK
	lv_demo_benchmark();
#else
	app_display_logo();

	display_start_timer = lv_timer_create(display_start, 5000, NULL);
	if (!display_start_timer) {
		display_start(NULL);
	}
#endif
	lvgl_unlock();

	int ret = display_blanking_off(display_dev);
	return ret == -ENOSYS ? 0 : ret;
}

void app_display_index()
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Welcome", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_START);

	lv_obj_t *hint_label = lv_label_create(container);
	lv_label_set_text(hint_label,
			  "OSKey (Open Source Key) is a fully open-source, "
			  "non-commercial hardware wallet project.\n\n"
			  "Please open \nhttps://oskey.xyz/settings \nin Chrome to connect.");
	lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(hint_label, lv_palette_main(LV_PALETTE_GREY), 0);
	lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hint_label, lv_disp_get_hor_res(NULL) - 40);
	lv_label_set_long_mode(hint_label, LV_LABEL_LONG_WRAP);

	lv_obj_t *tools_btn = lv_btn_create(lv_scr_act());
	lv_obj_align(tools_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
	lv_obj_set_style_bg_color(tools_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_bg_opa(tools_btn, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(tools_btn, 8, 0);

	lv_obj_t *btn_label = lv_label_create(tools_btn);
	lv_label_set_text(btn_label, "Tools");
	lv_obj_set_style_text_color(btn_label, lv_color_black(), 0);
	lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, 0);
	lv_obj_center(btn_label);
	lv_obj_add_event_cb(tools_btn, app_display_index_cb, LV_EVENT_CLICKED, NULL);
}

void app_display_init()
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Init", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	create_back_button(th, BACK_ACTION_TO_CHECK_FEATURES);
	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_CENTER);

	const char *labels[] = {"Generate", "Import"};
	app_init_action_t actions[] = {INIT_ACTION_GENERATE, INIT_ACTION_IMPORT};

	for (int i = 0; i < 2; i++) {
		lv_obj_t *btn = lv_btn_create(container);
		lv_obj_t *label = lv_label_create(btn);
		lv_label_set_text(label, labels[i]);
		lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
		lv_obj_set_style_text_color(label, lv_color_black(), 0);
		lv_obj_center(label);
		lv_obj_add_event_cb(btn, app_display_init_cb, LV_EVENT_CLICKED, (void *)actions[i]);
	}
}

static lv_style_t tools_btn_style, tools_btn_pressed_style;
static bool tools_styles_initialized = false;

void app_display_tools()
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Tools", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	create_back_button(th, BACK_ACTION_TO_INDEX);
	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_all(container, 0, 0);

	if (!tools_styles_initialized) {
		lv_style_init(&tools_btn_style);
		lv_style_set_bg_opa(&tools_btn_style, LV_OPA_TRANSP);
		lv_style_set_border_side(&tools_btn_style, LV_BORDER_SIDE_BOTTOM);
		lv_style_set_border_color(&tools_btn_style, lv_palette_main(LV_PALETTE_GREY));
		lv_style_set_border_width(&tools_btn_style, 1);
		lv_style_set_border_opa(&tools_btn_style, LV_OPA_40);
		lv_style_set_pad_all(&tools_btn_style, 0);
		lv_style_set_radius(&tools_btn_style, 0);

		lv_style_init(&tools_btn_pressed_style);
		lv_style_set_bg_opa(&tools_btn_pressed_style, LV_OPA_20);
		lv_style_set_bg_color(&tools_btn_pressed_style, lv_palette_main(LV_PALETTE_BLUE));

		tools_styles_initialized = true;
	}

	const char *labels[] = {"Erase data", "Restart"};
	app_tools_action_t actions[] = {TOOLS_ACTION_ERASE_DATA, TOOLS_ACTION_RESTART};
	int item_count = sizeof(labels) / sizeof(labels[0]);

	for (int i = 0; i < item_count; i++) {
		lv_obj_t *btn = lv_btn_create(container);
		lv_obj_add_style(btn, &tools_btn_style, 0);
		lv_obj_add_style(btn, &tools_btn_pressed_style, LV_STATE_PRESSED);
		lv_obj_set_size(btn, lv_disp_get_hor_res(NULL), 40);

		lv_obj_t *label = lv_label_create(btn);
		lv_label_set_text(label, labels[i]);
		lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
		lv_obj_set_style_text_color(label, lv_color_white(), 0);
		lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);

		lv_obj_add_event_cb(btn, app_display_tools_cb, LV_EVENT_CLICKED,
				    (void *)actions[i]);
	}
}

void app_display_mnemonic(int legth)
{
	size_t entropy_len = custom_mode ? legth * 4 / 3 : 0;

	display_submit(AppMessageAction_GenerateMnemonic, legth,
		       entropy_len > 0 ? entropy_bytes : NULL, entropy_len, NULL, 0);
	memset(entropy_bytes, 0, sizeof(entropy_bytes));
	custom_mode = false;
}

static void display_mnemonic(const uint8_t *data, size_t len)
{
	len = MIN(len, sizeof(check_mnemonic_buffer) - 1);
	if (len > 0) {
		memcpy(check_mnemonic_buffer, data, len);
	}
	check_mnemonic_buffer[len] = '\0';

	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Mnemonic", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	create_back_button(th, BACK_ACTION_TO_SELECT_LENGTH);

	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_START);
	lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_all(container, 10, 0);
	lv_obj_set_style_pad_row(container, 10, 0);
	lv_obj_set_style_pad_column(container, 10, 0);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t item_width = (screen_width - 50) / 2;

	const char *word = check_mnemonic_buffer;
	const char *end = check_mnemonic_buffer + len;

	for (int i = 0; i < 24 && word < end; i++) {
		while (word < end && *word == ' ') {
			word++;
		}

		const char *word_end = word;
		while (word_end < end && *word_end != ' ') {
			word_end++;
		}
		if (word == word_end) {
			break;
		}

		lv_obj_t *label = lv_label_create(container);
		lv_label_set_text_fmt(label, "%d. %.*s", i + 1, (int)(word_end - word), word);
		lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
		lv_obj_set_style_text_color(label, lv_color_white(), 0);
		lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
		lv_obj_set_style_border_width(label, 2, 0);
		lv_obj_set_style_border_opa(label, LV_OPA_50, 0);
		lv_obj_set_style_pad_all(label, 8, 0);
		lv_obj_set_width(label, item_width);

		word = word_end;
	}

	lv_obj_t *hint_label = lv_label_create(container);
	lv_label_set_text(hint_label, "Please write it down on a reliable medium such as paper.\n\n"
				      "Please note that the mnemonic phrase will only be displayed "
				      "once, so check it carefully.");
	lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(hint_label, lv_color_white(), 0);
	lv_obj_set_width(hint_label, LV_PCT(100));
	lv_obj_set_style_pad_top(hint_label, 20, 0);

	lv_obj_t *btn_cont = create_button_container(container);
	create_centered_button(btn_cont, "Ok", lv_palette_main(LV_PALETTE_BLUE), 100,
			       app_display_mnemonic_cb, NULL);
}

static void display_approve_cb(lv_event_t *event)
{
	ARG_UNUSED(event);
	app_confirmation_respond(true);
}

void app_display_index_cb(lv_event_t *e)
{
	app_display_tools();
}

void app_display_init_cb(lv_event_t *e)
{
	app_init_action_t action = (app_init_action_t)(intptr_t)lv_event_get_user_data(e);
	switch (action) {
	case INIT_ACTION_GENERATE:
		app_display_init_show_select_length();
		break;
	case INIT_ACTION_IMPORT:
		app_display_input("Import", INPUT_ACTION_IMPORT, BACK_ACTION_TO_INIT);
		break;
	}
}

void app_display_tools_cb(lv_event_t *e)
{
	app_tools_action_t action = (app_tools_action_t)(intptr_t)lv_event_get_user_data(e);
	if (action == TOOLS_ACTION_ERASE_DATA) {
		display_submit(AppMessageAction_ResetStorage, 0, NULL, 0, NULL, 0);
	} else if (action == TOOLS_ACTION_RESTART) {
		display_submit(AppMessageAction_Restart, 0, NULL, 0, NULL, 0);
	}
}

void back_button_event_handler(lv_event_t *e)
{
	app_back_action_t action = (app_back_action_t)(intptr_t)lv_event_get_user_data(e);

	switch (action) {
	case BACK_ACTION_TO_INIT:
		app_display_init();
		break;
	case BACK_ACTION_TO_CHECK_FEATURES:
		app_display_features();
		break;
	case BACK_ACTION_TO_SELECT_LENGTH:
		app_display_init_show_select_length();
		break;
	case BACK_ACTION_TO_INDEX:
		app_display_index();
		break;
	case BACK_ACTION_TO_TOOLS:
		app_display_tools();
		break;
	case BACK_ACTION_REJECT:
		app_confirmation_respond(false);
		break;
	default:
		break;
	}
}

static void app_display_init_show_select_length_cb(lv_event_t *e)
{
	app_mnemonic_length_t word_count =
		(app_mnemonic_length_t)(intptr_t)lv_event_get_user_data(e);
	app_display_mnemonic(word_count);
}

void app_display_init_show_custom_entropy_length_cb(lv_event_t *e)
{
	int custom_length = (int)(intptr_t)lv_event_get_user_data(e);
	app_display_entropy_collection(custom_length);
}

void app_display_mnemonic_cb()
{
	app_display_input("Check", INPUT_ACTION_CHECK_MNEMONIC, BACK_ACTION_TO_SELECT_LENGTH);
}

static void app_display_features_cb()
{
	app_display_input("Set Init PIN", INPUT_ACTION_PIN_SET, BACK_ACTION_TO_CHECK_FEATURES);
}

static void display_confirmation(const char *title_text, const char *button_text,
				 const uint8_t *data, size_t len)
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar(title_text, lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	create_back_button(th, BACK_ACTION_REJECT);
	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_START);
	lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_style_pad_all(container, 10, 0);

	lv_obj_t *msg_label = lv_label_create(container);
	lv_label_set_text_fmt(msg_label, "%.*s", (int)len, data ? (const char *)data : "");
	lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(msg_label, lv_color_white(), 0);
	lv_obj_set_width(msg_label, LV_PCT(100));
	lv_obj_set_style_pad_top(msg_label, 20, 0);

	lv_obj_t *btn_cont = create_button_container(container);
	create_centered_button(btn_cont, button_text, lv_palette_main(LV_PALETTE_RED), 100,
			       display_approve_cb, NULL);
}

static void display_sign(const uint8_t *data, size_t len)
{
	display_confirmation("Sign", "Sign", data, len);
}

static void display_confirm(const uint8_t *data, size_t len)
{
	static const uint8_t fallback[] = "Confirm security key request";

	if (data == NULL || len == 0) {
		data = fallback;
		len = sizeof(fallback) - 1;
	}
	display_confirmation("Confirm", "Approve", data, len);
}

static void display_ready(void)
{
	memset(check_mnemonic_buffer, 0, sizeof(check_mnemonic_buffer));
	memset(pin_buffer, 0, sizeof(pin_buffer));
	app_display_index();
}

static void display_error(AppError error, uint32_t value)
{
	lv_obj_t *label = create_error_label();

	switch (error) {
	case AppError_Busy:
		lv_label_set_text(label, "Device busy");
		break;
	case AppError_Rejected:
		lv_label_set_text(label, "Request rejected");
		break;
	case AppError_Locked:
		lv_label_set_text(label, "Wallet is locked");
		break;
	case AppError_NoPendingAction:
		lv_label_set_text(label, "No action pending");
		break;
	case AppError_DisplayRequired:
		lv_label_set_text(label, "Use the device display");
		break;
	case AppError_ExternalRequestRequired:
		lv_label_set_text(label, "External request required");
		break;
	case AppError_TrustedActionRequired:
		lv_label_set_text(label, "Trusted confirmation required");
		break;
	case AppError_InvalidAction:
		lv_label_set_text(label, "Invalid action");
		break;
	case AppError_UnlockFailed:
		lv_label_set_text_fmt(label, "Unlock failed (%u/10)", (unsigned int)value);
		break;
	case AppError_Unspecified:
	case AppError_Failed:
	default:
		lv_label_set_text(label, "Operation failed");
		break;
	}
}

void app_display_message(DisplayAction action, AppError error, uint32_t value, const uint8_t *data,
			 size_t len)
{
	lvgl_lock();

	if (display_start_timer) {
		lv_timer_delete(display_start_timer);
		display_start_timer = NULL;
	}

	switch (action) {
	case DisplayAction_Ready:
		display_ready();
		break;
	case DisplayAction_Mnemonic:
		display_mnemonic(data, len);
		break;
	case DisplayAction_Sign:
		display_sign(data, MIN(len, DISPLAY_SIGN_TEXT_MAX_LEN));
		break;
	case DisplayAction_Error:
		display_error(error, value);
		break;
	case DisplayAction_Confirm:
		display_confirm(data, len);
		break;
	case DisplayAction_Unspecified:
		break;
	}

	lvgl_unlock();
}

static void custom_button_toggle_cb(lv_event_t *e)
{
	custom_mode = true;
	app_display_init_show_select_length();
}

static void standard_button_toggle_cb(lv_event_t *e)
{
	custom_mode = false;
	app_display_init_show_select_length();
}

void app_display_init_show_select_length()
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Select", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	create_back_button(th, BACK_ACTION_TO_INIT);
	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_CENTER);

	if (!custom_mode) {
		lv_obj_t *spacer = lv_obj_create(container);
		lv_obj_set_size(spacer, LV_PCT(100), 40);
		lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(spacer, 0, 0);
		lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

		const char *labels[] = {"12 words", "18 words", "24 words"};
		app_mnemonic_length_t lengths[] = {MNEMONIC_LENGTH_12, MNEMONIC_LENGTH_18,
						   MNEMONIC_LENGTH_24};

		for (int i = 0; i < 3; i++) {
			lv_obj_t *btn = lv_btn_create(container);
			lv_obj_t *label = lv_label_create(btn);
			lv_label_set_text(label, labels[i]);
			lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
			lv_obj_set_style_text_color(label, lv_color_black(), 0);
			lv_obj_center(label);
			lv_obj_add_event_cb(btn, app_display_init_show_select_length_cb,
					    LV_EVENT_CLICKED, (void *)lengths[i]);
		}

		lv_obj_t *custom_btn = lv_btn_create(container);
		lv_obj_t *custom_label = lv_label_create(custom_btn);
		lv_label_set_text(custom_label, "Custom");
		lv_obj_set_style_text_font(custom_label, &lv_font_montserrat_18, 0);
		lv_obj_set_style_text_color(custom_label, lv_color_black(), 0);
		lv_obj_center(custom_label);
		lv_obj_add_event_cb(custom_btn, custom_button_toggle_cb, LV_EVENT_CLICKED, NULL);
	} else {
		lv_obj_t *btn2 = lv_btn_create(container);
		lv_obj_t *label2 = lv_label_create(btn2);
		lv_label_set_text(label2, "Custom Entropy 12 words");
		lv_obj_set_style_text_font(label2, &lv_font_montserrat_18, 0);
		lv_obj_set_style_text_color(label2, lv_color_black(), 0);
		lv_obj_center(label2);
		lv_obj_add_event_cb(btn2, app_display_init_show_custom_entropy_length_cb,
				    LV_EVENT_CLICKED, (void *)1);

		lv_obj_t *btn3 = lv_btn_create(container);
		lv_obj_t *label3 = lv_label_create(btn3);
		lv_label_set_text(label3, "Custom Entropy 24 words");
		lv_obj_set_style_text_font(label3, &lv_font_montserrat_18, 0);
		lv_obj_set_style_text_color(label3, lv_color_black(), 0);
		lv_obj_center(label3);
		lv_obj_add_event_cb(btn3, app_display_init_show_custom_entropy_length_cb,
				    LV_EVENT_CLICKED, (void *)2);

		lv_obj_t *btn4 = lv_btn_create(container);
		lv_obj_t *label4 = lv_label_create(btn4);
		lv_label_set_text(label4, "Back to standard");
		lv_obj_set_style_text_font(label4, &lv_font_montserrat_18, 0);
		lv_obj_set_style_text_color(label4, lv_color_black(), 0);
		lv_obj_center(label4);
		lv_obj_add_event_cb(btn4, standard_button_toggle_cb, LV_EVENT_CLICKED, NULL);
	}
}

void app_display_logo()
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);
	lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *label = lv_label_create(screen);
	lv_label_set_text(label, "OSKey");
	lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
	lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
}

static void keyboard_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *kb = lv_event_get_target(e);
	app_input_action_t action = (app_input_action_t)(intptr_t)lv_event_get_user_data(e);

	if (code == LV_EVENT_CANCEL) {
		lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
		resize_container_for_keyboard(false);
	}

	if (code == LV_EVENT_READY) {
		lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
		lv_obj_t *ta = lv_keyboard_get_textarea(kb);
		resize_container_for_keyboard(false);

		if (!ta) {
			return;
		}
		const char *text = lv_textarea_get_text(ta);

		switch (action) {
		case INPUT_ACTION_IMPORT:
			display_submit(AppMessageAction_InitCustom, 0, text, strlen(text),
				       pin_buffer, strlen(pin_buffer));
			break;

		case INPUT_ACTION_CHECK_MNEMONIC:
			if (strcmp(check_mnemonic_buffer, text) == 0 ||
			    strcmp(text, "oskey") == 0) {
				display_submit(AppMessageAction_InitCustom, 0,
					       check_mnemonic_buffer, strlen(check_mnemonic_buffer),
					       pin_buffer, strlen(pin_buffer));
			} else {
				show_fail(NULL);
			}
			break;

		case INPUT_ACTION_PIN_SET:
			if (!validate_pin_complexity(text)) {
				show_fail("Need 8+ chars: A-Z,a-z,0-9,!@#");
				return;
			}
			snprintf(pin_buffer, sizeof(pin_buffer), "%s", text);
			app_display_input("Check", INPUT_ACTION_PIN_CONFIRM,
					  BACK_ACTION_TO_CHECK_FEATURES);
			break;

		case INPUT_ACTION_PIN_CONFIRM:
			if (strcmp(pin_buffer, text) == 0) {
				app_display_init();
			} else {
				show_fail(NULL);
			}
			break;

		case INPUT_ACTION_PIN_VERIFY:
			display_submit(AppMessageAction_Unlock, 0, text, strlen(text), NULL, 0);
			break;
		}
	}
}

static void textarea_event_cb(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
		lv_obj_t *keyboard = (lv_obj_t *)lv_event_get_user_data(e);
		lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(keyboard);
		resize_container_for_keyboard(true);
	}
}

void app_display_input(char *title_text, uintptr_t action, uintptr_t back_action)
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar(title_text, lv_palette_main(LV_PALETTE_BLUE));
	title_height = lv_obj_get_height(title);

	if (back_action != BACK_ACTION_NONE) {
		create_back_button(title_height, back_action);
	}

	cont = create_content_container(title_height, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *text_area = lv_textarea_create(cont);
	lv_obj_set_scrollbar_mode(text_area, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_text_font(text_area, &lv_font_montserrat_18, 0);
	lv_obj_set_width(text_area, lv_pct(100));
	lv_obj_set_flex_grow(text_area, 1);
	lv_textarea_set_placeholder_text(text_area, "Enter words here...");
	if (action == INPUT_ACTION_PIN_SET || action == INPUT_ACTION_PIN_CONFIRM ||
	    action == INPUT_ACTION_PIN_VERIFY) {
		lv_textarea_set_max_length(text_area, sizeof(pin_buffer) - 1);
	}

	lv_obj_t *keyboard = lv_keyboard_create(lv_scr_act());
	lv_keyboard_set_textarea(keyboard, text_area);
	lv_obj_set_size(keyboard, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL) / 2);
	lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(keyboard);

	lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, (void *)action);
	lv_obj_add_event_cb(text_area, textarea_event_cb, LV_EVENT_CLICKED, keyboard);
}

void app_display_features()
{
	uint8_t features[7];
	if (!app_check_feature(features, sizeof(features))) {
		return;
	}

	const char *feature_descriptions[] = {
		"Secure Boot",  "Flash Encryption",  "Bootloader", "Storage",
		"Hardware Rng", "Display and Input", "User Button"};

	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Feature", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_row(container, 0, 0);
	lv_obj_set_style_pad_column(container, 0, 0);

	for (int i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
		if (i == 1) {
			continue;
		}

		lv_obj_t *feature_cont = lv_obj_create(container);
		lv_obj_set_size(feature_cont, LV_PCT(100), LV_SIZE_CONTENT);
		lv_obj_set_flex_flow(feature_cont, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(feature_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
				      LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_top(feature_cont, 6, 0);
		lv_obj_set_style_pad_bottom(feature_cont, 6, 0);
		lv_obj_set_style_bg_opa(feature_cont, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(feature_cont, 0, 0);

		lv_obj_t *symbol = lv_label_create(feature_cont);
		lv_label_set_text(symbol, features[i] ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
		lv_obj_set_width(symbol, 30);
		lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_text_color(symbol,
					    features[i] ? lv_palette_main(LV_PALETTE_GREEN)
							: lv_palette_main(LV_PALETTE_RED),
					    0);
		lv_obj_set_style_text_font(symbol, &lv_font_montserrat_14, 0);

		lv_obj_t *desc = lv_label_create(feature_cont);
		lv_label_set_text(desc, feature_descriptions[i]);
		lv_obj_set_style_text_color(desc, lv_color_white(), 0);
		lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
	}

	lv_obj_t *hint = lv_label_create(container);
	lv_label_set_text(hint, "Plesae check hardware support!");
	lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
	lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hint, LV_PCT(100));
	lv_obj_set_style_pad_top(hint, 10, 0);
	lv_obj_set_style_pad_bottom(hint, 10, 0);

	lv_obj_t *btn_cont = create_button_container(container);
	create_centered_button(btn_cont, "Checked", lv_palette_main(LV_PALETTE_BLUE), 100,
			       app_display_features_cb, NULL);

	lv_obj_t *spacer = lv_obj_create(container);
	lv_obj_set_size(spacer, LV_PCT(100), 50);
	lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(spacer, 0, 0);
}

void app_display_storage_error()
{
	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Storage Error", lv_palette_main(LV_PALETTE_RED));
	lv_coord_t th = lv_obj_get_height(title);

	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_CENTER);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);

	lv_obj_t *error_icon = lv_label_create(container);
	lv_label_set_text(error_icon, LV_SYMBOL_WARNING);
	lv_obj_set_style_text_font(error_icon, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(error_icon, lv_palette_main(LV_PALETTE_RED), 0);

	lv_obj_t *error_msg = lv_label_create(container);
	lv_label_set_text(error_msg, "Storage initialization failed!\n\n"
				     "Storage is supported but cannot be initialized properly. "
				     "This may be caused by corrupted data or hardware issues.");
	lv_obj_set_style_text_font(error_msg, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(error_msg, lv_color_white(), 0);
	lv_obj_set_style_text_align(error_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(error_msg, screen_width - 40);
	lv_label_set_long_mode(error_msg, LV_LABEL_LONG_WRAP);

	lv_obj_t *solution_msg = lv_label_create(container);
	lv_label_set_text(solution_msg, "Try erasing the flash storage to fix this issue.");
	lv_obj_set_style_text_font(solution_msg, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(solution_msg, lv_palette_main(LV_PALETTE_YELLOW), 0);
	lv_obj_set_style_text_align(solution_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(solution_msg, screen_width - 40);
	lv_label_set_long_mode(solution_msg, LV_LABEL_LONG_WRAP);

	lv_obj_t *btn_cont = lv_obj_create(container);
	lv_obj_set_size(btn_cont, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(btn_cont, 0, 0);
	lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(btn_cont, 0, 0);
	lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(btn_cont, 15, 0);

	lv_obj_t *erase_btn = lv_btn_create(btn_cont);
	lv_obj_set_size(erase_btn, 180, 40);
	lv_obj_set_style_bg_color(erase_btn, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_set_style_radius(erase_btn, 8, 0);
	lv_obj_t *erase_label = lv_label_create(erase_btn);
	lv_label_set_text(erase_label, "Erase Flash Storage");
	lv_obj_set_style_text_color(erase_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(erase_label, &lv_font_montserrat_16, 0);
	lv_obj_center(erase_label);
	lv_obj_add_event_cb(erase_btn, app_display_tools_cb, LV_EVENT_CLICKED,
			    (void *)TOOLS_ACTION_ERASE_DATA);

	lv_obj_t *restart_btn = lv_btn_create(btn_cont);
	lv_obj_set_size(restart_btn, 120, 40);
	lv_obj_set_style_bg_color(restart_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_radius(restart_btn, 8, 0);
	lv_obj_t *restart_label = lv_label_create(restart_btn);
	lv_label_set_text(restart_label, "Restart");
	lv_obj_set_style_text_color(restart_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(restart_label, &lv_font_montserrat_16, 0);
	lv_obj_center(restart_label);
	lv_obj_add_event_cb(restart_btn, app_display_tools_cb, LV_EVENT_CLICKED,
			    (void *)TOOLS_ACTION_RESTART);

	lv_obj_t *warning_msg = lv_label_create(container);
	lv_label_set_text(warning_msg, "Erasing flash will delete all data!");
	lv_obj_set_style_text_font(warning_msg, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(warning_msg, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_set_style_text_align(warning_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(warning_msg, screen_width - 40);
}

static uint8_t entropy_bits[8][32] = {0};
static int current_page = 0;
static int total_pages = 1;

static lv_obj_t *entropy_buttons[32] = {NULL};
static lv_obj_t *prev_button = NULL;
static lv_obj_t *next_button = NULL;
static lv_obj_t *confirm_button = NULL;
static lv_obj_t *progress_label = NULL;

static void update_entropy_button(int button_index)
{
	if (button_index < 0 || button_index >= 32) {
		return;
	}

	lv_obj_t *btn = entropy_buttons[button_index];
	if (!btn) {
		return;
	}

	lv_obj_t *label = lv_obj_get_child(btn, 0);
	if (!label) {
		return;
	}

	uint8_t bit_value = entropy_bits[current_page][button_index];
	lv_label_set_text(label, bit_value ? "1" : "0");
	lv_obj_set_style_bg_color(
		btn,
		bit_value ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_main(LV_PALETTE_GREY), 0);
}

static void entropy_button_event_cb(lv_event_t *e)
{
	int button_index = (int)(intptr_t)lv_event_get_user_data(e);
	entropy_bits[current_page][button_index] = !entropy_bits[current_page][button_index];
	update_entropy_button(button_index);
}

static void entropy_ui_update(void)
{
	if (progress_label) {
		char progress_text[32];
		snprintf(progress_text, sizeof(progress_text), "Page %d/%d", current_page + 1,
			 total_pages);
		lv_label_set_text(progress_label, progress_text);
	}

	for (int i = 0; i < 32; i++) {
		update_entropy_button(i);
	}

	bool is_first_page = (current_page == 0);
	bool is_last_page = (current_page == total_pages - 1);

	lv_obj_add_flag(prev_button, is_first_page ? LV_OBJ_FLAG_HIDDEN : 0);
	lv_obj_clear_flag(prev_button, is_first_page ? 0 : LV_OBJ_FLAG_HIDDEN);

	lv_obj_add_flag(next_button, is_last_page ? LV_OBJ_FLAG_HIDDEN : 0);
	lv_obj_clear_flag(next_button, is_last_page ? 0 : LV_OBJ_FLAG_HIDDEN);

	lv_obj_add_flag(confirm_button, is_last_page ? 0 : LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(confirm_button, is_last_page ? LV_OBJ_FLAG_HIDDEN : 0);
}

static void entropy_previous_event_cb(lv_event_t *e)
{
	if (current_page > 0) {
		current_page--;
		entropy_ui_update();
	}
}

static void entropy_next_event_cb(lv_event_t *e)
{
	if (current_page < total_pages - 1) {
		current_page++;
		entropy_ui_update();
	}
}

static void entropy_confirm_event_cb(lv_event_t *e)
{
	int total_bits = (int)(intptr_t)lv_event_get_user_data(e);
	int total_bytes = (total_bits + 7) / 8;

	memset(entropy_bytes, 0, sizeof(entropy_bytes));

	for (int page = 0; page < total_pages; page++) {
		for (int bit_idx = 0; bit_idx < 32; bit_idx++) {
			int global_bit_idx = page * 32 + bit_idx;
			if (global_bit_idx >= total_bits) {
				break;
			}

			int byte_idx = global_bit_idx / 8;
			int bit_offset = global_bit_idx % 8;

			if (entropy_bits[page][bit_idx]) {
				entropy_bytes[byte_idx] |= (1 << (7 - bit_offset));
			}
		}
	}

	app_display_mnemonic(total_bytes / 4 * 3);

	current_page = 0;
}

void app_display_entropy_collection(int page_param)
{
	memset(entropy_bits, 0, sizeof(entropy_bits));
	current_page = 0;
	total_pages = page_param * 4;

	lv_obj_clean(lv_scr_act());

	lv_obj_t *title = create_title_bar("Custom Entropy", lv_palette_main(LV_PALETTE_BLUE));
	lv_coord_t th = lv_obj_get_height(title);

	create_back_button(th, BACK_ACTION_TO_SELECT_LENGTH);

	lv_obj_t *container = create_content_container(th, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_all(container, 10, 0);
	lv_obj_set_style_pad_row(container, 5, 0);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t button_size = (screen_width - 100) / 8;

	for (int row = 0; row < 4; row++) {
		lv_obj_t *row_cont = lv_obj_create(container);
		lv_obj_set_size(row_cont, LV_PCT(100), button_size);
		lv_obj_set_flex_flow(row_cont, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(row_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
				      LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_border_width(row_cont, 0, 0);
		lv_obj_set_style_bg_opa(row_cont, LV_OPA_TRANSP, 0);
		lv_obj_set_style_pad_all(row_cont, 0, 0);

		for (int col = 0; col < 8; col++) {
			int button_index = row * 8 + col;

			lv_obj_t *btn = lv_btn_create(row_cont);
			entropy_buttons[button_index] = btn;

			lv_obj_set_size(btn, button_size, button_size);
			lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
			lv_obj_set_style_radius(btn, 4, 0);

			lv_obj_t *label = lv_label_create(btn);
			lv_label_set_text(label, "0");
			lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
			lv_obj_set_style_text_color(label, lv_color_black(), 0);
			lv_obj_center(label);

			lv_obj_add_event_cb(btn, entropy_button_event_cb, LV_EVENT_CLICKED,
					    (void *)(intptr_t)button_index);
		}
	}

	lv_obj_t *nav_btn_cont = create_button_container(container);

	prev_button = lv_btn_create(nav_btn_cont);
	lv_obj_set_size(prev_button, 80, 40);
	lv_obj_set_style_bg_color(prev_button, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_radius(prev_button, 8, 0);

	lv_obj_t *prev_label = lv_label_create(prev_button);
	lv_label_set_text(prev_label, "Previous");
	lv_obj_set_style_text_color(prev_label, lv_color_black(), 0);
	lv_obj_center(prev_label);

	lv_obj_add_event_cb(prev_button, entropy_previous_event_cb, LV_EVENT_CLICKED, NULL);

	next_button = lv_btn_create(nav_btn_cont);
	lv_obj_set_size(next_button, 80, 40);
	lv_obj_set_style_bg_color(next_button, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_radius(next_button, 8, 0);

	lv_obj_t *next_label = lv_label_create(next_button);
	lv_label_set_text(next_label, "Next");
	lv_obj_set_style_text_color(next_label, lv_color_black(), 0);
	lv_obj_center(next_label);

	lv_obj_add_event_cb(next_button, entropy_next_event_cb, LV_EVENT_CLICKED, NULL);

	confirm_button = lv_btn_create(nav_btn_cont);
	lv_obj_set_size(confirm_button, 80, 40);
	lv_obj_set_style_bg_color(confirm_button, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_radius(confirm_button, 8, 0);

	lv_obj_t *confirm_label = lv_label_create(confirm_button);
	lv_label_set_text(confirm_label, "Confirm");
	lv_obj_set_style_text_color(confirm_label, lv_color_black(), 0);
	lv_obj_center(confirm_label);

	int entropy_length = total_pages * 32;
	lv_obj_add_event_cb(confirm_button, entropy_confirm_event_cb, LV_EVENT_CLICKED,
			    (void *)(intptr_t)entropy_length);

	progress_label = lv_label_create(container);
	lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(progress_label, lv_color_white(), 0);
	lv_obj_set_style_text_align(progress_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(progress_label, LV_PCT(100));
	lv_obj_set_style_pad_top(progress_label, 15, 0);
	lv_obj_set_style_pad_bottom(progress_label, 10, 0);

	entropy_ui_update();
}

static void display_start(lv_timer_t *timer)
{
	if (timer) {
		lv_timer_delete(timer);
		display_start_timer = NULL;
	}

	uint8_t features[7];
	app_check_feature(features, sizeof(features));

	if (features[3] && !app_check_storage()) {
		app_display_storage_error();
	} else if (storage_general_check(storage_ids.seed)) {
		app_display_input("Verify PIN", INPUT_ACTION_PIN_VERIFY, BACK_ACTION_NONE);
	} else {
		app_display_features();
	}
}

#else

int app_init_display(void)
{
	return 0;
}

void app_display_message(DisplayAction action, AppError error, uint32_t value, const uint8_t *data,
			 size_t len)
{
	(void)action;
	(void)error;
	(void)value;
	(void)data;
	(void)len;
}

#endif
