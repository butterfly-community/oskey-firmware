#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"

static bool valid_pin(const char *pin)
{
	bool digit = false;
	bool lower = false;
	bool upper = false;
	bool symbol = false;

	if (strlen(pin) < 8) {
		return false;
	}

	for (; *pin != '\0'; ++pin) {
		bool is_digit = *pin >= '0' && *pin <= '9';
		bool is_lower = *pin >= 'a' && *pin <= 'z';
		bool is_upper = *pin >= 'A' && *pin <= 'Z';
		digit |= is_digit;
		lower |= is_lower;
		upper |= is_upper;
		symbol |= *pin >= 32 && *pin <= 126 && !is_digit && !is_lower && !is_upper;
	}
	return digit && lower && upper && symbol;
}

static void input_changed(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (ui.input_error != NULL) {
		lv_obj_add_flag(ui.input_error, LV_OBJ_FLAG_HIDDEN);
	}
}

static void keyboard_done(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CANCEL) {
		ui_keyboard_hide();
		return;
	}
	if (lv_event_get_code(event) != LV_EVENT_READY || ui.input == NULL) {
		return;
	}

	ui_keyboard_hide();
	const char *text = lv_textarea_get_text(ui.input);
	switch (ui.page) {
	case UI_PAGE_LOCKED:
		ui_submit(AppMessageAction_Unlock, 0, text, strlen(text), NULL, 0);
		break;
	case UI_PAGE_PIN_NEW:
		if (!valid_pin(text)) {
			ui_input_error("Use 8+ characters with upper, lower, number and symbol");
			return;
		}
		snprintf(ui.pin, sizeof(ui.pin), "%s", text);
		ui_push(UI_PAGE_PIN_CONFIRM);
		break;
	case UI_PAGE_PIN_CONFIRM:
		if (strcmp(ui.pin, text) != 0) {
			ui_input_error("PINs do not match");
			return;
		}
		ui_push(UI_PAGE_SOURCE);
		break;
	case UI_PAGE_IMPORT:
		ui_submit(AppMessageAction_InitCustom, 0, text, strlen(text), ui.pin,
			  strlen(ui.pin));
		break;
	case UI_PAGE_VERIFY:
		/* Entering "oskey" instead of the phrase is an intentional product option. */
		if (strcmp(text, "oskey") != 0 && strcmp(ui.mnemonic, text) != 0) {
			ui_input_error("Recovery phrase does not match");
			return;
		}
		ui_submit(AppMessageAction_InitCustom, 0, ui.mnemonic, strlen(ui.mnemonic), ui.pin,
			  strlen(ui.pin));
		break;
	default:
		break;
	}
}

static void input_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_keyboard_show();
}

static void password_toggled(lv_event_t *event)
{
	if (ui.input == NULL) {
		return;
	}
	bool hidden = lv_textarea_get_password_mode(ui.input);
	lv_textarea_set_password_mode(ui.input, !hidden);
	lv_image_set_src(lv_event_get_user_data(event), hidden ? &oskey_eye_off : &oskey_eye);
}

static void show_input(const char *title, const char *hint, bool password)
{
	lv_obj_t *content = ui_page_begin(title, ui.page == UI_PAGE_LOCKED ? UI_NAVIGATION_NONE
									   : UI_NAVIGATION_BACK);
	lv_obj_t *description = lv_label_create(content);
	lv_obj_set_width(description, LV_PCT(100));
	lv_obj_set_style_text_color(description, lv_color_hex(0x929eaa), 0);
	lv_obj_set_style_text_font(description, &lv_font_montserrat_12, 0);
	lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
	lv_label_set_text(description, hint);

	lv_obj_t *form = lv_obj_create(content);
	lv_obj_set_size(form, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(form, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(form, 0, 0);
	lv_obj_set_style_radius(form, 0, 0);
	lv_obj_set_style_pad_all(form, 0, 0);
	lv_obj_set_style_pad_row(form, 4, 0);
	lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
	lv_obj_remove_flag(form, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					 LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *field = lv_obj_create(form);
	lv_obj_set_size(field, LV_PCT(100), password ? 40 : LV_MIN(88, ui.height / 4));
	lv_obj_set_style_bg_opa(field, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(field, lv_color_hex(0x484848), 0);
	lv_obj_set_style_border_width(field, 1, 0);
	lv_obj_set_style_border_color(field, lv_color_hex(0x4da3ff), LV_STATE_FOCUSED);
	lv_obj_set_style_radius(field, 3, 0);
	lv_obj_set_style_pad_all(field, 0, 0);
	lv_obj_set_style_pad_column(field, 0, 0);
	lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(field, input_clicked, LV_EVENT_CLICKED, NULL);

	ui.input = lv_textarea_create(field);
	lv_obj_set_height(ui.input, LV_PCT(100));
	lv_obj_set_width(ui.input, password ? 0 : LV_PCT(100));
	if (password) {
		lv_obj_set_flex_grow(ui.input, 1);
	}
	lv_textarea_set_one_line(ui.input, password);
	lv_textarea_set_password_mode(ui.input, password);
	lv_textarea_set_max_length(ui.input, password ? UI_PIN_SIZE - 1 : UI_MNEMONIC_SIZE - 1);
	lv_textarea_set_placeholder_text(ui.input, password ? "Enter PIN" : "word1 word2 ...");
	lv_obj_set_style_text_font(ui.input, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_color(ui.input, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_bg_color(ui.input, lv_color_hex(0x4da3ff),
				  LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(ui.input, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(ui.input, lv_color_hex(0xf2f5f7),
				    LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(ui.input, lv_color_hex(0x727e89), LV_PART_TEXTAREA_PLACEHOLDER);
	lv_obj_set_style_text_font(ui.input, &lv_font_montserrat_12, LV_PART_TEXTAREA_PLACEHOLDER);
	lv_obj_set_style_bg_opa(ui.input, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(ui.input, 0, 0);
	lv_obj_set_style_radius(ui.input, 0, 0);
	lv_obj_set_style_pad_all(ui.input, 6, 0);
	lv_obj_add_event_cb(ui.input, input_changed, LV_EVENT_VALUE_CHANGED, NULL);
	if (password) {
		lv_obj_t *button = lv_button_create(field);
		lv_obj_set_size(button, 44, 38);
		lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
		lv_obj_set_style_bg_color(button, lv_color_hex(0xffffff), LV_STATE_PRESSED);
		lv_obj_set_style_bg_opa(button, LV_OPA_10, LV_STATE_PRESSED);
		lv_obj_set_style_border_width(button, 0, 0);
		lv_obj_set_style_shadow_width(button, 0, 0);
		lv_obj_set_ext_click_area(button, 3);
		lv_obj_t *eye = ui_icon(button, &oskey_eye);
		ui_icon_color(eye, ui_tone_color(UI_TONE_MUTED));
		lv_obj_center(eye);
		lv_obj_add_event_cb(button, password_toggled, LV_EVENT_CLICKED, eye);
	}

	ui.input_error = lv_label_create(form);
	lv_obj_set_width(ui.input_error, LV_PCT(100));
	lv_obj_set_style_text_color(ui.input_error, lv_color_hex(0xe36a78), 0);
	lv_obj_set_style_text_font(ui.input_error, &lv_font_montserrat_12, 0);
	lv_label_set_long_mode(ui.input_error, LV_LABEL_LONG_WRAP);
	lv_obj_add_flag(ui.input_error, LV_OBJ_FLAG_HIDDEN);

	ui.keyboard = lv_keyboard_create(ui.screen);
	lv_obj_set_size(ui.keyboard, LV_PCT(100), LV_MIN(ui.height * 45 / 100, 180));
	lv_obj_align(ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_bg_opa(ui.keyboard, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(ui.keyboard, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(ui.keyboard, lv_palette_lighten(LV_PALETTE_GREY, 2),
				  LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(ui.keyboard, LV_OPA_COVER, LV_PART_ITEMS);
	lv_obj_set_style_text_color(ui.keyboard, lv_palette_darken(LV_PALETTE_GREY, 4),
				    LV_PART_ITEMS);
	lv_obj_set_style_border_color(ui.keyboard, lv_color_hex(0x303944), LV_PART_ITEMS);
	lv_obj_set_style_border_width(ui.keyboard, 1, LV_PART_ITEMS);
	lv_obj_set_style_shadow_width(ui.keyboard, 0, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(ui.keyboard, lv_palette_lighten(LV_PALETTE_GREY, 2),
				  LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_text_color(ui.keyboard, lv_palette_darken(LV_PALETTE_GREY, 4),
				    LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_bg_color(ui.keyboard, lv_palette_lighten(LV_PALETTE_GREY, 1),
				  LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(ui.keyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_border_color(ui.keyboard, lv_color_hex(0x4da3ff),
				      LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_text_color(ui.keyboard, lv_palette_darken(LV_PALETTE_GREY, 4),
				    LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_text_font(ui.keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
	lv_obj_add_event_cb(ui.keyboard, keyboard_done, LV_EVENT_ALL, NULL);
	lv_obj_add_event_cb(ui.input, input_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_add_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void navigate(lv_event_t *event)
{
	ui_push((enum ui_page)(uintptr_t)lv_event_get_user_data(event));
}

static void generate_mnemonic(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui.custom_entropy = false;
	ui_push(UI_PAGE_LENGTH);
}

static void enable_custom_entropy(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui.custom_entropy = true;
	ui_render();
}

static void select_mnemonic_length(lv_event_t *event)
{
	uint32_t words = (uint32_t)(uintptr_t)lv_event_get_user_data(event);

	ui_submit(AppMessageAction_GenerateMnemonic, words, NULL, 0, NULL, 0);
}

static void select_entropy_size(lv_event_t *event)
{
	ui.entropy_bits = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
	ui_wipe(ui.entropy, sizeof(ui.entropy));
	ui_push(UI_PAGE_ENTROPY);
}

static void restart_device(void)
{
	ui_submit(AppMessageAction_Restart, 0, NULL, 0, NULL, 0);
}

static void erase_storage(void)
{
	ui_submit(AppMessageAction_ResetStorage, 0, NULL, 0, NULL, 0);
}

static void confirm_restart(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_dialog_show(&oskey_refresh, "Restart OSKey?",
		       "The device will disconnect briefly. Stored data will not change.",
		       "Restart", UI_TONE_DEFAULT, restart_device);
}

static void confirm_reset(lv_event_t *event)
{
	ARG_UNUSED(event);
	bool all_storage = ui.page == UI_PAGE_STORAGE_ERROR;

	ui_dialog_show(&oskey_trash, all_storage ? "Erase storage?" : "Erase wallet?",
		       all_storage ? "All device data will be permanently removed."
				   : "Back up the recovery phrase first. This cannot be undone.",
		       all_storage ? "Erase storage" : "Erase wallet", UI_TONE_DANGER,
		       erase_storage);
}

static void show_splash(void)
{
	lv_obj_t *content = ui_page_begin("", UI_NAVIGATION_NONE);
	lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_t *logo = ui_icon(content, &oskey_wallet);
	lv_obj_set_size(logo, 60, 60);
	lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CONTAIN);
	lv_obj_t *name = lv_label_create(content);
	lv_obj_set_width(name, LV_PCT(100));
	lv_obj_set_style_text_color(name, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(name, "OSKey");
	lv_obj_add_flag(ui.status_bar, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
	lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);
}

static void show_capabilities(void)
{
	static const char *const names[APP_FEATURE_COUNT] = {
		[APP_FEATURE_SECURE_BOOT] = "Secure boot",
		[APP_FEATURE_FLASH_ENCRYPTION] = "Flash encryption",
		[APP_FEATURE_BOOTLOADER] = "Bootloader",
		[APP_FEATURE_STORAGE] = "Storage",
		[APP_FEATURE_HARDWARE_RNG] = "Hardware RNG",
		[APP_FEATURE_DISPLAY_INPUT] = "Display and input",
		[APP_FEATURE_USER_BUTTON] = "User button",
	};

	ui_clear_sensitive();
	lv_obj_t *content = ui_page_begin("OSKey capabilities", UI_NAVIGATION_NONE);
	ui_section(content, "CAPABILITIES");
	for (size_t i = 0; i < ARRAY_SIZE(names); ++i) {
		bool enabled = ui.startup.features[i];
		ui_list_row(content, enabled ? &oskey_success : &oskey_failure, names[i], NULL,
			    NULL, enabled ? UI_TONE_SUCCESS : UI_TONE_MUTED, NULL, NULL);
	}
	ui_section(content, "SETUP");
	ui_list_row(content, &oskey_wallet, "Set up OSKey", "Create or restore a wallet", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_PIN_NEW);
}

static void show_locked(void)
{
	show_input("Unlock OSKey", "Enter the PIN for this wallet", true);
	ui_list_row(ui.content, &oskey_warning, "Failed PIN attempts can erase the wallet",
		    "Wallet data is erased after 10 failed attempts", NULL, UI_TONE_WARNING, NULL,
		    NULL);
	ui_section(ui.content, "RECOVERY");
	ui_list_row(ui.content, &oskey_trash, "Erase wallet", "Remove wallet data and start again",
		    NULL, UI_TONE_DANGER, confirm_reset, NULL);
}

static void show_home(void)
{
	lv_obj_t *content = ui_page_begin("OSKey", UI_NAVIGATION_NONE);
	ui_clear_sensitive();

	ui_list_row(content, &oskey_wallet, "Hardware wallet", "USB, Bluetooth or UART", NULL,
		    UI_TONE_ACTIVE, NULL, NULL);
	ui_list_row(content, &oskey_passkey, "Passkeys", "FIDO2 over USB", NULL, UI_TONE_ACTIVE,
		    NULL, NULL);
	ui_list_row(content, &oskey_settings, "Device settings", NULL, NULL, UI_TONE_DEFAULT,
		    navigate, (void *)(uintptr_t)UI_PAGE_SETTINGS);
}

static void show_settings(void)
{
	lv_obj_t *content = ui_page_begin("Device settings", UI_NAVIGATION_BACK);
	ui_section(content, "MAINTENANCE");
	ui_list_row(content, &oskey_refresh, "Restart", "Restart without changing data", NULL,
		    UI_TONE_DEFAULT, confirm_restart, NULL);
	ui_list_row(content, &oskey_trash, "Erase wallet", "Remove wallet data permanently", NULL,
		    UI_TONE_DANGER, confirm_reset, NULL);
}

static void show_source(void)
{
	lv_obj_t *content = ui_page_begin("Create wallet", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_document, "Choose a recovery source",
		    "Generate a new phrase or restore one", NULL, UI_TONE_DEFAULT, NULL, NULL);
	ui_section(content, "RECOVERY SOURCE");
	ui_list_row(content, &oskey_wallet, "Generate recovery phrase",
		    "Create a new wallet with hardware randomness", NULL, UI_TONE_DEFAULT,
		    generate_mnemonic, NULL);
	ui_list_row(content, &oskey_document, "Import recovery phrase",
		    "Restore an existing wallet", NULL, UI_TONE_DEFAULT, navigate,
		    (void *)(uintptr_t)UI_PAGE_IMPORT);
}

static void show_length(void)
{
	lv_obj_t *content = ui_page_begin(ui.custom_entropy ? "Custom entropy" : "Recovery phrase",
					  UI_NAVIGATION_BACK);
	ui_list_row(content, ui.custom_entropy ? &oskey_shuffle : &oskey_document,
		    ui.custom_entropy ? "Choose entropy size" : "Choose recovery length",
		    ui.custom_entropy ? "Every bit can be entered on screen"
				      : "Longer phrases provide more entropy",
		    NULL, UI_TONE_DEFAULT, NULL, NULL);
	if (ui.custom_entropy) {
		ui_section(content, "ENTROPY SIZE");
		ui_list_row(content, NULL, "12 words", "128-bit entropy", NULL, UI_TONE_DEFAULT,
			    select_entropy_size, (void *)(uintptr_t)128);
		ui_list_row(content, NULL, "24 words", "256-bit entropy", NULL, UI_TONE_DEFAULT,
			    select_entropy_size, (void *)(uintptr_t)256);
		return;
	}

	ui_section(content, "WORD COUNT");
	ui_list_row(content, NULL, "12 words", "128-bit entropy", NULL, UI_TONE_DEFAULT,
		    select_mnemonic_length, (void *)(uintptr_t)12);
	ui_list_row(content, NULL, "18 words", "192-bit entropy", NULL, UI_TONE_DEFAULT,
		    select_mnemonic_length, (void *)(uintptr_t)18);
	ui_list_row(content, NULL, "24 words", "256-bit entropy", NULL, UI_TONE_DEFAULT,
		    select_mnemonic_length, (void *)(uintptr_t)24);
	ui_section(content, "ADVANCED");
	ui_list_row(content, &oskey_shuffle, "Enter custom entropy", NULL, NULL, UI_TONE_DEFAULT,
		    enable_custom_entropy, NULL);
}

static void mnemonic_saved(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_push(UI_PAGE_VERIFY);
}

static void show_mnemonic(void)
{
	lv_obj_t *content = ui_page_begin("Recovery phrase", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_document, "Write these words down",
		    "Keep them offline and in order", NULL, UI_TONE_WARNING, NULL, NULL);
	ui_section(content, "RECOVERY WORDS");

	lv_obj_t *words = lv_obj_create(content);
	lv_obj_set_size(words, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(words, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(words, 0, 0);
	lv_obj_set_style_pad_all(words, 0, 0);
	lv_obj_set_style_pad_column(words, 10, 0);
	lv_obj_set_flex_flow(words, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_remove_flag(words, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					  LV_OBJ_FLAG_SCROLLABLE);

	uint8_t index = 0;
	const char *word = ui.mnemonic;
	while (*word != '\0') {
		while (*word == ' ') {
			++word;
		}
		if (*word == '\0') {
			break;
		}
		const char *end = strchr(word, ' ');
		size_t len = end == NULL ? strlen(word) : (size_t)(end - word);
		char row[40];
		snprintf(row, sizeof(row), "%2u  %.*s", ++index, (int)len, word);

		lv_obj_t *label = lv_label_create(words);
		lv_obj_set_width(label, LV_PCT(48));
		lv_obj_set_height(label, 30);
		lv_obj_set_style_text_color(label, lv_color_hex(0xf2f5f7), 0);
		lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
		lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(label, lv_color_hex(0x242b33), 0);
		lv_obj_set_style_border_width(label, 1, 0);
		lv_obj_set_style_pad_ver(label, 5, 0);
		lv_label_set_text(label, row);
		word = end == NULL ? word + len : end + 1;
	}

	ui_section(content, "WHEN FINISHED");
	ui_list_row(content, &oskey_success, "I saved the recovery phrase",
		    "Continue to verification", NULL, UI_TONE_SUCCESS, mnemonic_saved, NULL);
}

static uint8_t entropy_columns(void)
{
	return ui.width < 360 ? 4 : 8;
}

static void entropy_click(lv_event_t *event)
{
	lv_obj_t *table = lv_event_get_target_obj(event);
	uint32_t row;
	uint32_t column;
	lv_table_get_selected_cell(table, &row, &column);
	uint8_t columns = entropy_columns();
	uint16_t bit = row * columns + column;
	if (bit >= ui.entropy_bits) {
		return;
	}
	ui.entropy[bit / 8] ^= BIT(7 - bit % 8);
	lv_table_set_cell_value(table, row, column,
				ui.entropy[bit / 8] & BIT(7 - bit % 8) ? "1" : "0");
}

static void entropy_finish(lv_event_t *event)
{
	ARG_UNUSED(event);
	uint8_t entropy[sizeof(ui.entropy)];
	size_t len = ui.entropy_bits / 8;
	memcpy(entropy, ui.entropy, len);
	ui_submit(AppMessageAction_GenerateMnemonic, len / 4 * 3, entropy, len, NULL, 0);
	ui_wipe(entropy, sizeof(entropy));
}

static void show_entropy(void)
{
	uint8_t columns = entropy_columns();
	lv_obj_t *content = ui_page_begin("Custom entropy", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_shuffle, "Set each entropy bit", "Tap a bit to toggle 0 or 1",
		    NULL, UI_TONE_DEFAULT, NULL, NULL);
	char section[16];
	snprintf(section, sizeof(section), "%u BITS", ui.entropy_bits);
	ui_section(content, section);

	lv_obj_t *table = lv_table_create(content);
	lv_obj_set_width(table, LV_PCT(100));
	lv_obj_set_height(table, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(table, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS);
	lv_obj_set_style_border_color(table, lv_color_hex(0x303944), LV_PART_ITEMS);
	lv_obj_set_style_text_color(table, lv_color_hex(0xb8c1ca), LV_PART_ITEMS);
	lv_obj_set_style_text_font(table, &lv_font_montserrat_12, LV_PART_ITEMS);
	lv_obj_set_style_text_align(table, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);
	lv_obj_set_style_pad_ver(table, 15, LV_PART_ITEMS);
	lv_obj_set_style_pad_hor(table, 0, LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_border_color(table, lv_color_hex(0x4da3ff),
				      LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_text_color(table, lv_color_hex(0x4da3ff),
				    LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_table_set_row_count(table, DIV_ROUND_UP(ui.entropy_bits, columns));
	lv_table_set_column_count(table, columns);
	lv_obj_update_layout(content);
	int32_t column_width = lv_obj_get_content_width(content) / columns;
	for (uint8_t column = 0; column < columns; ++column) {
		lv_table_set_column_width(table, column, column_width);
	}
	for (uint16_t bit = 0; bit < ui.entropy_bits; ++bit) {
		lv_table_set_cell_value(table, bit / columns, bit % columns,
					ui.entropy[bit / 8] & BIT(7 - bit % 8) ? "1" : "0");
	}
	lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(table, entropy_click, LV_EVENT_VALUE_CHANGED, NULL);

	ui_section(content, "WHEN FINISHED");
	ui_list_row(content, &oskey_document, "Generate recovery phrase", "Use this entropy", NULL,
		    UI_TONE_ACTIVE, entropy_finish, NULL);
}

static void show_storage_error(void)
{
	lv_obj_t *content = ui_page_begin("Storage unavailable", UI_NAVIGATION_NONE);
	ui_list_row(content, &oskey_warning, "Secure storage could not be opened",
		    "Restart first; erase only if the problem continues", NULL, UI_TONE_WARNING,
		    NULL, NULL);
	ui_section(content, "RECOVERY ACTIONS");
	ui_list_row(content, &oskey_refresh, "Restart", "Try opening storage again", NULL,
		    UI_TONE_DEFAULT, confirm_restart, NULL);
	ui_list_row(content, &oskey_trash, "Erase storage", "Remove all device data", NULL,
		    UI_TONE_DANGER, confirm_reset, NULL);
}

void ui_render(void)
{
	switch (ui.page) {
	case UI_PAGE_SPLASH:
		show_splash();
		break;
	case UI_PAGE_CAPABILITIES:
		show_capabilities();
		break;
	case UI_PAGE_LOCKED:
		show_locked();
		break;
	case UI_PAGE_HOME:
		show_home();
		break;
	case UI_PAGE_SETTINGS:
		show_settings();
		break;
	case UI_PAGE_PIN_NEW:
		show_input("Create PIN", "Use upper, lower, number and symbol", true);
		break;
	case UI_PAGE_PIN_CONFIRM:
		show_input("Confirm PIN", "Enter the same PIN again", true);
		break;
	case UI_PAGE_SOURCE:
		show_source();
		break;
	case UI_PAGE_LENGTH:
		show_length();
		break;
	case UI_PAGE_IMPORT:
		show_input("Import wallet", "Enter the recovery phrase in order", false);
		break;
	case UI_PAGE_MNEMONIC:
		show_mnemonic();
		break;
	case UI_PAGE_VERIFY:
		show_input("Verify phrase", "Enter the recovery phrase again", false);
		break;
	case UI_PAGE_ENTROPY:
		show_entropy();
		break;
	case UI_PAGE_STORAGE_ERROR:
		show_storage_error();
		break;
	case UI_PAGE_NONE:
		break;
	}
	lv_obj_update_layout(ui.content);
	lv_obj_scroll_to(ui.content, 0, 0, LV_ANIM_OFF);
}

void ui_show_startup(void)
{
	switch (ui.startup.state) {
	case APP_DISPLAY_LOCKED:
		ui_open(UI_PAGE_LOCKED);
		break;
	case APP_DISPLAY_STORAGE_ERROR:
		ui_open(UI_PAGE_STORAGE_ERROR);
		break;
	case APP_DISPLAY_SETUP:
	default:
		ui_open(UI_PAGE_CAPABILITIES);
		break;
	}
}
