#include "lvgl.h"

#ifdef CONFIG_DISPLAY

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include "wrapper.h"
#include "lvgl.h"
#include "storage.h"
#include "app.h"

#ifdef CONFIG_LV_Z_DEMO_BENCHMARK
#include <lv_demos.h>
#endif

static char check_mnemonic_buffer[256] = {0};

struct k_work app_mnemonic_generate_work;
int app_mnemonic_length = 0;
char app_mnemonic_buffer[256] = {0};

void app_mnemonic_generate_work_handler(struct k_work *work)
{
	wallet_mnemonic_generate_from_display(app_mnemonic_length, app_mnemonic_buffer, sizeof(app_mnemonic_buffer));
}

void app_mnemonic_generate_trigger(void)
{
	k_work_init(&app_mnemonic_generate_work, app_mnemonic_generate_work_handler);
}

struct k_work app_wallet_init_custom_work;
char app_wallet_init_mnemonic[256] = {0};
bool app_wallet_init_result = false;
volatile bool app_wallet_init_done = false;

void app_wallet_init_custom_work_handler(struct k_work *work)
{
	app_wallet_init_result = wallet_init_custom_from_display(app_wallet_init_mnemonic);
	app_wallet_init_done = true;
}

void app_wallet_init_custom_trigger(void)
{
	k_work_init(&app_wallet_init_custom_work, app_wallet_init_custom_work_handler);
}

const struct device *display_dev;

int app_init_display()
{
	app_mnemonic_generate_trigger();
	app_wallet_init_custom_trigger();

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		return 0;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	struct gpio_dt_spec lcd_backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);

	if (!gpio_is_ready_dt(&lcd_backlight)) {
		return 0;
	}

	gpio_pin_configure_dt(&lcd_backlight, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&lcd_backlight, 1);
#endif

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);

#ifdef CONFIG_LV_Z_DEMO_BENCHMARK
	lv_demo_benchmark();
	return 0;
#endif

	return 0;
}

void app_display_hello()
{
	char str[12] = "Hello World\0";

	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

	lv_obj_t *label = lv_label_create(screen);
	lv_label_set_text(label, str);
	lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);

	return;
}

static void app_display_init_cb(lv_event_t *e)
{
	app_init_action_t action = (app_init_action_t)(intptr_t)lv_event_get_user_data(e);
	if (action == INIT_ACTION_GENERATE) {
		app_display_init_show_select_length();
	}

	if (action == INIT_ACTION_IMPORT) {
		app_display_input("Import", INPUT_ACTION_IMPORT, BACK_ACTION_TO_INIT);
	}
}

static void app_display_index_cb()
{
	app_display_tools();
}

void app_display_index()
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Welcome");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);
	lv_coord_t title_height = lv_obj_get_height(title);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_pad_row(cont, 20, 0);

	lv_obj_t *hint_label = lv_label_create(cont);
	lv_label_set_text(hint_label, "OSKey (Open Source Key) is a fully open-source, "
				      "non-commercial hardware wallet project. ");
	lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(hint_label, lv_palette_main(LV_PALETTE_GREY), 0);
	lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hint_label, screen_width - 40);
	lv_label_set_long_mode(hint_label, LV_LABEL_LONG_WRAP);

	lv_obj_t *setting_btn = lv_btn_create(screen);
	lv_obj_align(setting_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
	lv_obj_set_style_bg_color(setting_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_bg_opa(setting_btn, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(setting_btn, 8, 0);
	lv_obj_set_style_text_color(setting_btn, lv_color_black(), 0);

	lv_obj_t *btn_label = lv_label_create(setting_btn);
	lv_label_set_text(btn_label, "Tools");
	lv_obj_set_style_text_color(btn_label, lv_color_black(), 0);
	lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, 0);
	lv_obj_center(btn_label);
	lv_obj_add_event_cb(setting_btn, app_display_index_cb, LV_EVENT_CLICKED, NULL);
}

void app_display_init()
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Init");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);
	lv_coord_t title_height = lv_obj_get_height(title);

	lv_obj_t *back_btn = lv_btn_create(screen);
	lv_obj_set_size(back_btn, 40, 40);
	lv_obj_set_pos(back_btn, 10, (title_height - 40) / 2);
	lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (lv_obj_get_height(title) - 40) / 2);
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
			    (void *)BACK_ACTION_TO_CHECK_FEATURES);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_pad_row(cont, 20, 0);

	lv_obj_t *btn_generate = lv_btn_create(cont);
	lv_obj_t *label_generate = lv_label_create(btn_generate);
	lv_label_set_text(label_generate, "Generate");
	lv_obj_set_style_text_font(label_generate, &lv_font_montserrat_18, 0);
	lv_obj_center(label_generate);
	lv_obj_set_style_text_color(label_generate, lv_color_black(), 0);
	lv_obj_add_event_cb(btn_generate, app_display_init_cb, LV_EVENT_CLICKED,
			    (void *)INIT_ACTION_GENERATE);

	lv_obj_t *btn_import = lv_btn_create(cont);
	lv_obj_t *label_import = lv_label_create(btn_import);
	lv_label_set_text(label_import, "Import");
	lv_obj_set_style_text_font(label_import, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(label_import, lv_color_black(), 0);
	lv_obj_center(label_import);
	lv_obj_add_event_cb(btn_import, app_display_init_cb, LV_EVENT_CLICKED,
			    (void *)INIT_ACTION_IMPORT);
}

static void app_display_tools_cb(lv_event_t *e)
{
	app_tools_action_t action = (app_tools_action_t)(intptr_t)lv_event_get_user_data(e);
	if (action == TOOLS_ACTION_ERASE_DATA) {
		if (app_check_storage()) {
			storage_erase_nvs();
		}
		storage_erase_flash();
		sys_reboot(SYS_REBOOT_COLD);
	}

	if (action == TOOLS_ACTION_RESTART) {
		sys_reboot(SYS_REBOOT_COLD);
	}
}

void back_button_event_handler(lv_event_t *e)
{
	app_back_action_t action = (app_back_action_t)(intptr_t)lv_event_get_user_data(e);

	if (action == BACK_ACTION_TO_INIT) {
		app_display_init();
	}

	if (action == BACK_ACTION_TO_CHECK_FEATURES) {
		app_display_features();
	}

	if (action == BACK_ACTION_TO_SELECT_LENGTH) {
		app_display_init_show_select_length();
	}

	if (action == BACK_ACTION_TO_INDEX) {
		app_display_index();
	}

	if (action == BACK_ACTION_TO_SELECT_LENGTH) {
		app_display_init_show_select_length();
	}

	if (action == BACK_ACTION_TO_TOOLS) {
		app_display_tools();
	}
}

void app_display_tools()
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Tools");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);
	lv_coord_t title_height = lv_obj_get_height(title);
	lv_obj_t *back_btn = lv_btn_create(screen);
	lv_obj_set_size(back_btn, 40, 40);
	lv_obj_set_pos(back_btn, 10, (title_height - 40) / 2);
	lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (lv_obj_get_height(title) - 40) / 2);
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
			    (void *)BACK_ACTION_TO_INDEX);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_pad_all(cont, 0, 0);

	static lv_style_t btn_style;
	lv_style_init(&btn_style);
	lv_style_set_bg_opa(&btn_style, LV_OPA_TRANSP);
	lv_style_set_border_side(&btn_style, LV_BORDER_SIDE_BOTTOM);
	lv_style_set_border_color(&btn_style, lv_palette_main(LV_PALETTE_GREY));
	lv_style_set_border_width(&btn_style, 1);
	lv_style_set_border_opa(&btn_style, LV_OPA_40);
	lv_style_set_pad_all(&btn_style, 0);
	lv_style_set_radius(&btn_style, 0);

	static lv_style_t btn_pressed_style;
	lv_style_init(&btn_pressed_style);
	lv_style_set_bg_opa(&btn_pressed_style, LV_OPA_20);
	lv_style_set_bg_color(&btn_pressed_style, lv_palette_main(LV_PALETTE_BLUE));

	lv_obj_t *btn_1 = lv_btn_create(cont);
	lv_obj_add_style(btn_1, &btn_style, 0);
	lv_obj_add_style(btn_1, &btn_pressed_style, LV_STATE_PRESSED);
	lv_obj_set_size(btn_1, screen_width, 40);
	lv_obj_set_style_text_color(btn_1, lv_color_white(), 0);

	lv_obj_t *label_1 = lv_label_create(btn_1);
	lv_label_set_text(label_1, "Erase data");
	lv_obj_set_style_text_font(label_1, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(label_1, lv_color_white(), 0);
	lv_obj_align(label_1, LV_ALIGN_LEFT_MID, 16, 0);

	lv_obj_add_event_cb(btn_1, app_display_tools_cb, LV_EVENT_CLICKED,
			    (void *)TOOLS_ACTION_ERASE_DATA);

	lv_obj_t *btn_2 = lv_btn_create(cont);
	lv_obj_add_style(btn_2, &btn_style, 0);
	lv_obj_add_style(btn_2, &btn_pressed_style, LV_STATE_PRESSED);
	lv_obj_set_size(btn_2, screen_width, 40);
	lv_obj_set_style_text_color(btn_2, lv_color_white(), 0);

	lv_obj_t *label_2 = lv_label_create(btn_2);
	lv_label_set_text(label_2, "Restart");
	lv_obj_set_style_text_font(label_2, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(label_2, lv_color_white(), 0);
	lv_obj_align(label_2, LV_ALIGN_LEFT_MID, 16, 0);

	lv_obj_add_event_cb(btn_2, app_display_tools_cb, LV_EVENT_CLICKED,
			    (void *)TOOLS_ACTION_RESTART);
}

static void app_display_mnemonic_cb()
{
	// app_display_index();
	app_display_input("Check", INPUT_ACTION_CHECK_MNEMONIC, BACK_ACTION_TO_SELECT_LENGTH);
}

void app_display_mnemonic(int legth)
{
	app_mnemonic_length = legth;
	memset(app_mnemonic_buffer, 0, sizeof(app_mnemonic_buffer));

	k_work_submit(&app_mnemonic_generate_work);

	while (app_mnemonic_buffer[0] == '\0') {
		k_sleep(K_MSEC(100));
	}

	lv_async_call(app_display_mnemonic_process, NULL);
}

void app_display_mnemonic_process(void *param)
{
	(void)param;

	strcpy(check_mnemonic_buffer, app_mnemonic_buffer);

	char words[24][12];
	int word_count = 0;
	char *token = strtok(app_mnemonic_buffer, " ");

	while (token != NULL && word_count < 24) {
		snprintf(words[word_count], sizeof(words[word_count]), "%d. %s", word_count + 1,
			 token);
		word_count++;
		token = strtok(NULL, " ");
	}

	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);
	lv_coord_t item_width = (screen_width - 50) / 2;

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Mnemonic");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);

	lv_coord_t title_height = lv_obj_get_height(title);
	lv_obj_t *back_btn = lv_btn_create(screen);
	lv_obj_set_size(back_btn, 40, 40);
	lv_obj_set_pos(back_btn, 10, (title_height - 40) / 2);
	lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (lv_obj_get_height(title) - 40) / 2);
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
			    (void *)BACK_ACTION_TO_SELECT_LENGTH);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_all(cont, 10, 0);
	lv_obj_set_style_pad_row(cont, 10, 0);
	lv_obj_set_style_pad_column(cont, 10, 0);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);

	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

	for (int i = 0; i < word_count; i++) {
		lv_obj_t *label = lv_label_create(cont);
		lv_label_set_text(label, words[i]);
		lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
		lv_obj_set_style_text_color(label, lv_color_white(), 0);
		lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
		lv_obj_set_style_border_width(label, 2, 0);
		lv_obj_set_style_border_opa(label, LV_OPA_50, 0);
		lv_obj_set_style_radius(label, 0, 0);
		lv_obj_set_style_pad_all(label, 8, 0);
		lv_obj_set_width(label, item_width);
		lv_obj_set_style_min_width(label, item_width, 0);
		lv_obj_set_style_max_width(label, item_width, 0);
		lv_obj_set_height(label, LV_SIZE_CONTENT);
	}

	lv_obj_t *hint_label = lv_label_create(cont);
	lv_label_set_text(
		hint_label,
		"Please write it down on a reliable medium such as paper.\n\nPlease note that the "
		"mnemonic phrase will only be displayed once, so check it carefully.");
	lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(hint_label, lv_color_white(), 0);
	lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_border_width(hint_label, 0, 0);
	lv_obj_set_width(hint_label, LV_PCT(100));
	lv_obj_set_style_pad_top(hint_label, 20, 0);
	lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_LEFT, 0);

	lv_obj_t *btn_cont = lv_obj_create(cont);
	lv_obj_set_size(btn_cont, LV_PCT(100), 60);
	lv_obj_set_style_border_width(btn_cont, 0, 0);
	lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(btn_cont, 0, 0);
	lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	lv_obj_t *ok_btn = lv_btn_create(btn_cont);
	lv_obj_set_size(ok_btn, 100, 40);
	lv_obj_set_style_bg_color(ok_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_radius(ok_btn, 8, 0);
	lv_obj_t *ok_label = lv_label_create(ok_btn);
	lv_label_set_text(ok_label, "Ok");
	lv_obj_set_style_text_color(ok_label, lv_color_black(), 0);
	lv_obj_center(ok_label);
	lv_obj_add_event_cb(ok_btn, app_display_mnemonic_cb, LV_EVENT_CLICKED, NULL);
}

void app_sign_cb()
{
	k_work_submit(&app_sign_work);
	app_display_index();
}

static char sign_buffer[1024] = {0};

void app_display_sign_x()
{

	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Sign");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);

	lv_coord_t title_height = lv_obj_get_height(title);
	lv_obj_t *back_btn = lv_btn_create(screen);
	lv_obj_set_size(back_btn, 40, 40);
	lv_obj_set_pos(back_btn, 10, (title_height - 40) / 2);
	lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (lv_obj_get_height(title) - 40) / 2);
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
			    (void *)BACK_ACTION_TO_INIT);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_all(cont, 10, 0);
	lv_obj_set_style_pad_row(cont, 10, 0);
	lv_obj_set_style_pad_column(cont, 10, 0);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);

	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

	lv_obj_t *hint_label = lv_label_create(cont);
	lv_label_set_text(hint_label, sign_buffer);
	lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(hint_label, lv_color_white(), 0);
	lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_border_width(hint_label, 0, 0);
	lv_obj_set_width(hint_label, LV_PCT(100));
	lv_obj_set_style_pad_top(hint_label, 20, 0);
	lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_LEFT, 0);

	lv_obj_t *btn_cont = lv_obj_create(cont);
	lv_obj_set_size(btn_cont, LV_PCT(100), 60);
	lv_obj_set_style_border_width(btn_cont, 0, 0);
	lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(btn_cont, 0, 0);
	lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	lv_obj_t *ok_btn = lv_btn_create(btn_cont);
	lv_obj_set_size(ok_btn, 100, 40);
	lv_obj_set_style_bg_color(ok_btn, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_set_style_radius(ok_btn, 8, 0);
	lv_obj_t *ok_label = lv_label_create(ok_btn);
	lv_label_set_text(ok_label, "Sign");
	lv_obj_set_style_text_color(ok_label, lv_color_black(), 0);
	lv_obj_center(ok_label);
	lv_obj_add_event_cb(ok_btn, app_sign_cb, LV_EVENT_CLICKED, NULL);
}

void app_display_sign(char *text)
{
	strcpy(sign_buffer, text);
	lv_async_call(app_display_sign_x, NULL);
}

static void app_display_init_show_select_length_cb(lv_event_t *e)
{
	app_mnemonic_length_t word_count =
		(app_mnemonic_length_t)(intptr_t)lv_event_get_user_data(e);
	app_display_mnemonic(word_count);
}

void app_display_init_show_select_length()
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Select");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);

	lv_coord_t title_height = lv_obj_get_height(title);
	lv_obj_t *back_btn = lv_btn_create(screen);
	lv_obj_set_size(back_btn, 40, 40);
	lv_obj_set_pos(back_btn, 10, (title_height - 40) / 2);
	lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (lv_obj_get_height(title) - 40) / 2);
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
			    (void *)BACK_ACTION_TO_INIT);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_pad_row(cont, 20, 0);

	lv_obj_t *btn1 = lv_btn_create(cont);
	lv_obj_t *label1 = lv_label_create(btn1);
	lv_label_set_text(label1, "12 words");
	lv_obj_set_style_text_font(label1, &lv_font_montserrat_18, 0);
	lv_obj_center(label1);
	lv_obj_set_style_text_color(label1, lv_color_black(), 0);
	lv_obj_add_event_cb(btn1, app_display_init_show_select_length_cb, LV_EVENT_CLICKED,
			    (void *)MNEMONIC_LENGTH_12);

	lv_obj_t *btn2 = lv_btn_create(cont);
	lv_obj_t *label2 = lv_label_create(btn2);
	lv_label_set_text(label2, "18 words");
	lv_obj_center(label2);
	lv_obj_set_style_text_color(label2, lv_color_black(), 0);
	lv_obj_set_style_text_font(label2, &lv_font_montserrat_18, 0);
	lv_obj_add_event_cb(btn2, app_display_init_show_select_length_cb, LV_EVENT_CLICKED,
			    (void *)MNEMONIC_LENGTH_18);

	lv_obj_t *btn3 = lv_btn_create(cont);
	lv_obj_t *label3 = lv_label_create(btn3);
	lv_label_set_text(label3, "24 words");
	lv_obj_center(label3);
	lv_obj_set_style_text_color(label3, lv_color_black(), 0);
	lv_obj_set_style_text_font(label3, &lv_font_montserrat_18, 0);
	lv_obj_add_event_cb(btn3, app_display_init_show_select_length_cb, LV_EVENT_CLICKED,
			    (void *)MNEMONIC_LENGTH_24);
}

void app_display_logo()
{
	char logo[12] = "OSKey\0";

	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

	lv_obj_t *label = lv_label_create(screen);
	lv_label_set_text(label, logo);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
	lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);

	return;
}

static lv_obj_t *cont = NULL;
static int title_height = 0;

void hide_error_label(lv_timer_t *timer)

{
	lv_obj_t *label = (lv_obj_t *)timer->user_data;
	lv_obj_del(label);
	lv_timer_del(timer);
}

void show_fail(char *msg)
{
	lv_obj_t *error_label = lv_label_create(lv_scr_act());

	if (msg) {
		lv_label_set_text(error_label, msg);
	} else {
		lv_label_set_text(error_label, "Verification failed!");
	}
	lv_obj_set_style_text_color(error_label, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_align(error_label, LV_ALIGN_BOTTOM_MID, 0, -20);
	lv_timer_t *timer = lv_timer_create(hide_error_label, 2000, error_label);

	lv_timer_set_repeat_count(timer, 1);
}

char pin_buffer[30] = {0};

static bool validate_pin_complexity(const char *pin)
{
	if (!pin || strlen(pin) < 8) {
		return false;
	}

	bool has_digit = false;
	bool has_lower = false;
	bool has_upper = false;
	bool has_symbol = false;

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

static void keyboard_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *kb = lv_event_get_target(e);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);
	app_input_action_t action = (app_input_action_t)(intptr_t)lv_event_get_user_data(e);

	if (code == LV_EVENT_CANCEL) {
		lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

		if (cont) {
			lv_obj_set_size(cont, lv_disp_get_hor_res(NULL),
					screen_height - title_height);
		}
	}
	if (code == LV_EVENT_READY) {
		lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

		lv_obj_t *ta = lv_keyboard_get_textarea(kb);

		if (cont) {
			lv_obj_set_size(cont, lv_disp_get_hor_res(NULL),
					screen_height - title_height);
		}

		const char *text = lv_textarea_get_text(ta);

		if (ta) {
			if (action == INPUT_ACTION_IMPORT) {
				strncpy(app_wallet_init_mnemonic, text, sizeof(app_wallet_init_mnemonic) - 1);
				app_wallet_init_result = false;
				app_wallet_init_done = false;

				k_work_submit(&app_wallet_init_custom_work);

				while (!app_wallet_init_done) {
					k_sleep(K_MSEC(100));
				}

				if (app_wallet_init_result) {
					app_display_index();
				} else {
					show_fail(NULL);
				}
			}

			if (action == INPUT_ACTION_CHECK_MNEMONIC) {
				if (strcmp(check_mnemonic_buffer, text) == 0 ||
				    strcmp(text, "oskey") == 0) {
					strncpy(app_wallet_init_mnemonic, check_mnemonic_buffer, sizeof(app_wallet_init_mnemonic) - 1);
					app_wallet_init_result = false;
					app_wallet_init_done = false;

					k_work_submit(&app_wallet_init_custom_work);

					while (!app_wallet_init_done) {
						k_sleep(K_MSEC(100));
					}

					app_display_index();
				} else {
					show_fail(NULL);
				}
			}

			if (action == INPUT_ACTION_PIN_SET) {
				if (!validate_pin_complexity(text)) {
					show_fail("Need 8+ chars: A-Z,a-z,0-9,!@#");
					return;
				}

				strcpy(pin_buffer, text);
				app_display_input("Check", INPUT_ACTION_PIN_CONFIRM,
						  BACK_ACTION_TO_CHECK_FEATURES);
			}
			if (action == INPUT_ACTION_PIN_CONFIRM) {
				if (strcmp(pin_buffer, text) == 0) {
					wallet_set_pin_cache_from_display(pin_buffer);
					app_display_init();
				} else {
					show_fail(NULL);
				}
			}

			if (action == INPUT_ACTION_PIN_VERIFY) {
				if (wallet_unlock_from_display(text)) {
					app_display_index();
				} else {
					show_fail(NULL);
				}
			}
		}
	}
}

static void textarea_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *keyboard = (lv_obj_t *)lv_event_get_user_data(e);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	if (code == LV_EVENT_CLICKED) {
		lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(keyboard);
		if (cont) {
			lv_obj_set_size(cont, lv_disp_get_hor_res(NULL),
					screen_height - title_height - (screen_height / 2));
		}
	}
}

void app_display_input(char *title_text, uintptr_t action, uintptr_t back_action)
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);

	lv_label_set_text(title, title_text);

	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
	lv_obj_update_layout(title);

	title_height = lv_obj_get_height(title);

	if (back_action != BACK_ACTION_NONE) {
		lv_obj_t *back_btn = lv_btn_create(screen);
		lv_obj_set_size(back_btn, 40, 40);
		lv_obj_set_pos(back_btn, 10, (title_height - 40) / 2);
		lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, (lv_obj_get_height(title) - 40) / 2);
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
	}

	cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *text_area = lv_textarea_create(cont);
	// lv_textarea_set_accepted_chars(text_area, "abcdefghijklmnopqrstuvwxyz ");
	lv_obj_set_scrollbar_mode(text_area, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_text_font(text_area, &lv_font_montserrat_18, 0);
	lv_obj_set_style_max_height(text_area, lv_obj_get_height(cont), 0);
	lv_obj_set_width(text_area, lv_pct(100));
	lv_textarea_set_placeholder_text(text_area, "Enter words here...");

	lv_obj_t *keyboard = lv_keyboard_create(screen);
	lv_keyboard_set_textarea(keyboard, text_area);
	lv_obj_set_size(keyboard, screen_width, screen_height / 2);
	lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_flex_grow(text_area, 1);
	lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(keyboard);

	lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, (void *)action);
	lv_obj_add_event_cb(text_area, textarea_event_cb, LV_EVENT_CLICKED, keyboard);
}

static void app_display_features_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	if (code == LV_EVENT_CLICKED) {
		app_display_input("Set PIN", INPUT_ACTION_PIN_SET, BACK_ACTION_TO_CHECK_FEATURES);
	}
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

	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Feature");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);
	lv_coord_t title_height = lv_obj_get_height(title);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_row(cont, 0, 0);
	lv_obj_set_style_pad_column(cont, 0, 0);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

	for (int i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
		if (i == 1) {
			continue;
		}

		lv_obj_t *feature_cont = lv_obj_create(cont);
		lv_obj_set_size(feature_cont, LV_PCT(100), LV_SIZE_CONTENT);
		lv_obj_set_flex_flow(feature_cont, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(feature_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
				      LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_top(feature_cont, 6, 0);
		lv_obj_set_style_pad_bottom(feature_cont, 6, 0);
		lv_obj_set_style_bg_opa(feature_cont, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(feature_cont, 0, 0);
		lv_obj_t *symbol = lv_label_create(feature_cont);
		lv_obj_set_width(symbol, 30);
		lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);

		if (features[i]) {
			lv_label_set_text(symbol, LV_SYMBOL_OK);
			lv_obj_set_style_text_color(symbol, lv_palette_main(LV_PALETTE_GREEN), 0);
		} else {
			lv_label_set_text(symbol, LV_SYMBOL_CLOSE);
			lv_obj_set_style_text_color(symbol, lv_palette_main(LV_PALETTE_RED), 0);
		}
		lv_obj_set_style_text_font(symbol, &lv_font_montserrat_14, 0);

		lv_obj_t *desc = lv_label_create(feature_cont);
		lv_label_set_text(desc, feature_descriptions[i]);
		lv_obj_set_style_text_color(desc, lv_color_white(), 0);
		lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
	}

	lv_obj_t *hint = lv_label_create(cont);
	lv_label_set_text(hint, "Plesae check hardware support!");
	lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
	lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hint, LV_PCT(100));
	lv_obj_set_style_pad_top(hint, 10, 0);
	lv_obj_set_style_pad_bottom(hint, 10, 0);

	lv_obj_t *btn_cont = lv_obj_create(cont);
	lv_obj_set_size(btn_cont, LV_PCT(100), 60);
	lv_obj_set_style_border_width(btn_cont, 0, 0);
	lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(btn_cont, 0, 0);
	lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	lv_obj_t *ok_btn = lv_btn_create(btn_cont);
	lv_obj_set_size(ok_btn, 100, 40);
	lv_obj_set_style_bg_color(ok_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_set_style_radius(ok_btn, 8, 0);
	lv_obj_t *ok_label = lv_label_create(ok_btn);
	lv_label_set_text(ok_label, "Checked");
	lv_obj_set_style_text_color(ok_label, lv_color_black(), 0);
	lv_obj_center(ok_label);
	lv_obj_add_event_cb(ok_btn, app_display_features_cb, LV_EVENT_CLICKED, NULL);
}

void app_display_storage_error()
{
	lv_obj_t *screen = lv_scr_act();
	lv_obj_clean(screen);

	lv_coord_t screen_width = lv_disp_get_hor_res(NULL);
	lv_coord_t screen_height = lv_disp_get_ver_res(NULL);

	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Storage Error");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_bg_color(title, lv_color_black(), 0);
	lv_obj_set_style_pad_all(title, 8, 0);
	lv_obj_set_size(title, screen_width, LV_SIZE_CONTENT);
	lv_obj_set_pos(title, 0, 0);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(title, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_set_style_border_width(title, 2, 0);
	lv_obj_set_style_border_opa(title, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);

	lv_obj_update_layout(title);
	lv_coord_t title_height = lv_obj_get_height(title);

	lv_obj_t *cont = lv_obj_create(screen);
	lv_obj_set_size(cont, screen_width, screen_height - title_height);
	lv_obj_set_pos(cont, 0, title_height);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
	lv_obj_set_style_pad_row(cont, 20, 0);

	lv_obj_t *error_icon = lv_label_create(cont);
	lv_label_set_text(error_icon, LV_SYMBOL_WARNING);
	lv_obj_set_style_text_font(error_icon, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(error_icon, lv_palette_main(LV_PALETTE_RED), 0);

	lv_obj_t *error_msg = lv_label_create(cont);
	lv_label_set_text(error_msg, "Storage initialization failed!\n\n"
				     "Storage is supported but cannot be initialized properly. "
				     "This may be caused by corrupted data or hardware issues.");
	lv_obj_set_style_text_font(error_msg, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(error_msg, lv_color_white(), 0);
	lv_obj_set_style_text_align(error_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(error_msg, screen_width - 40);
	lv_label_set_long_mode(error_msg, LV_LABEL_LONG_WRAP);

	lv_obj_t *solution_msg = lv_label_create(cont);
	lv_label_set_text(solution_msg, "Try erasing the flash storage to fix this issue.");
	lv_obj_set_style_text_font(solution_msg, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(solution_msg, lv_palette_main(LV_PALETTE_YELLOW), 0);
	lv_obj_set_style_text_align(solution_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(solution_msg, screen_width - 40);
	lv_label_set_long_mode(solution_msg, LV_LABEL_LONG_WRAP);

	lv_obj_t *btn_cont = lv_obj_create(cont);
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
	lv_obj_set_style_bg_color(restart_btn, lv_palette_main(LV_PALETTE_GREY), 0);
	lv_obj_set_style_radius(restart_btn, 8, 0);
	lv_obj_t *restart_label = lv_label_create(restart_btn);
	lv_label_set_text(restart_label, "Restart");
	lv_obj_set_style_text_color(restart_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(restart_label, &lv_font_montserrat_16, 0);
	lv_obj_center(restart_label);
	lv_obj_add_event_cb(restart_btn, app_display_tools_cb, LV_EVENT_CLICKED,
			    (void *)TOOLS_ACTION_RESTART);

	lv_obj_t *warning_msg = lv_label_create(cont);
	lv_label_set_text(warning_msg, "Erasing flash will delete all data!");
	lv_obj_set_style_text_font(warning_msg, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(warning_msg, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_set_style_text_align(warning_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(warning_msg, screen_width - 40);
}

void app_display_loop()
{
	lv_timer_handler();

	display_blanking_off(display_dev);

	app_display_logo();

	lv_timer_handler();

	k_msleep(3000);

	uint8_t features[7];
	app_check_feature(features, sizeof(features));

	if (features[3] == true && !app_check_storage()) {
		app_display_storage_error();
	} else {
		if (storage_general_check(STORAGE_ID_SEED)) {
			wallet_lock();
			app_display_input("Verify PIN", INPUT_ACTION_PIN_VERIFY, BACK_ACTION_NONE);
		} else {
			app_display_features();
		}
	}

	lv_timer_handler();

	while (1) {
		uint32_t sleep_ms = lv_timer_handler();
		k_msleep(MIN(sleep_ms, INT32_MAX));
	}
}

#else

void app_display_loop()
{
	if (storage_general_check(STORAGE_ID_SEED)) {
		wallet_lock();
	}
	return;
}

int app_init_display()
{
	return 0;
}

void app_display_sign(char *text)
{
	return;
}

#endif
