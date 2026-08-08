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

static const struct ui_input_config input_pages[] = {
	[UI_PAGE_LOCKED] =
		{
			.title = "Unlock OSKey",
			.hint = "Enter the PIN for this wallet",
			.placeholder = "Enter PIN",
			.action = "Unlock",
			.action_detail = "Open the hardware wallet",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_PIN_NEW] =
		{
			.title = "Create PIN",
			.hint = "Use upper, lower, number and symbol",
			.placeholder = "Enter PIN",
			.action = "Continue",
			.action_detail = "Confirm this PIN",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_PIN_CONFIRM] =
		{
			.title = "Confirm PIN",
			.hint = "Enter the same PIN again",
			.placeholder = "Enter PIN",
			.action = "Continue",
			.action_detail = "Choose a recovery source",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_FIDO_PIN_RECOVER] =
		{
			.title = "Recover FIDO PIN",
			.hint = "Verify the OSKey wallet PIN",
			.placeholder = "Wallet PIN",
			.action = "Recover",
			.action_detail = "Restore FIDO PIN attempts",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_IMPORT] =
		{
			.title = "Import wallet",
			.hint = "Enter the recovery phrase in order",
			.placeholder = "word1 word2 ...",
			.action = "Continue",
			.action_detail = "Configure the mnemonic passphrase",
			.max_length = UI_MNEMONIC_SIZE - 1,
		},
	[UI_PAGE_VERIFY] =
		{
			.title = "Verify phrase",
			.hint = "Enter the recovery phrase again",
			.placeholder = "word1 word2 ...",
			.action = "Continue",
			.action_detail = "Configure the mnemonic passphrase",
			.max_length = UI_MNEMONIC_SIZE - 1,
		},
	[UI_PAGE_PASSPHRASE] =
		{
			.title = "Mnemonic passphrase",
			.hint = "Optional; leave empty to continue without one",
			.placeholder = "Can be empty",
			.action = "Continue",
			.action_detail = "Use this passphrase or leave it empty",
			.max_length = UI_PASSPHRASE_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_PASSPHRASE_CONFIRM] =
		{
			.title = "Confirm passphrase",
			.hint = "Enter the same mnemonic passphrase",
			.placeholder = "Repeat passphrase",
			.action = "Continue",
			.action_detail = "Create the wallet",
			.max_length = UI_PASSPHRASE_SIZE - 1,
			.password = true,
		},
};

static void submit_wallet(void)
{
	char auxiliary[UI_PASSPHRASE_SIZE + UI_PIN_SIZE];
	size_t passphrase_len = strlen(ui.passphrase);
	size_t pin_len = strlen(ui.pin);

	memcpy(auxiliary, ui.passphrase, passphrase_len);
	memcpy(auxiliary + passphrase_len, ui.pin, pin_len);
	ui_submit(LocalRequestKind_InitCustom, (uint32_t)passphrase_len, ui.mnemonic,
		  strlen(ui.mnemonic), auxiliary, passphrase_len + pin_len);
	ui_wipe(auxiliary, sizeof(auxiliary));
}

static void submit_current_input(const char *text)
{
	switch (ui.page) {
	case UI_PAGE_LOCKED:
	case UI_PAGE_FIDO_PIN_RECOVER:
		ui_submit(LocalRequestKind_Unlock, 0, text, strlen(text), NULL, 0);
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
		snprintf(ui.mnemonic, sizeof(ui.mnemonic), "%s", text);
		ui_push(UI_PAGE_PASSPHRASE);
		break;
	case UI_PAGE_VERIFY:
		/* Entering "oskey" instead of the phrase is an intentional product option. */
		if (strcmp(text, "oskey") != 0 && strcmp(ui.mnemonic, text) != 0) {
			ui_input_error("Recovery phrase does not match");
			return;
		}
		ui_push(UI_PAGE_PASSPHRASE);
		break;
	case UI_PAGE_PASSPHRASE:
		ui_wipe(ui.passphrase, sizeof(ui.passphrase));
		snprintf(ui.passphrase, sizeof(ui.passphrase), "%s", text);
		if (text[0] == '\0') {
			submit_wallet();
		} else {
			ui_push(UI_PAGE_PASSPHRASE_CONFIRM);
		}
		break;
	case UI_PAGE_PASSPHRASE_CONFIRM:
		if (strcmp(ui.passphrase, text) != 0) {
			ui_input_error("Passphrases do not match");
			return;
		}
		submit_wallet();
		break;
	case UI_PAGE_WIFI_PASSWORD:
		ui_wifi_password_submit(text);
		break;
	default:
		break;
	}
}

static void navigate(lv_event_t *event)
{
	ui_push((enum ui_page)(uintptr_t)lv_event_get_user_data(event));
}

static void open_mnemonic_length(void)
{
	ui.custom_entropy = false;
	ui_push(UI_PAGE_LENGTH);
}

static void generate_mnemonic(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (!ui.features[APP_FEATURE_HARDWARE_RNG]) {
		ui_dialog_show(&oskey_warning, "No hardware RNG",
			       "Test only. Do not use this phrase for real assets.", "Continue",
			       UI_TONE_WARNING, open_mnemonic_length);
		return;
	}

	open_mnemonic_length();
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

	ui_submit(LocalRequestKind_GenerateMnemonic, words, NULL, 0, NULL, 0);
}

static void select_entropy_size(lv_event_t *event)
{
	ui.entropy_bits = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
	ui_wipe(ui.entropy, sizeof(ui.entropy));
	ui_push(UI_PAGE_ENTROPY);
}

static void restart_device(void)
{
	ui_submit(LocalRequestKind_Restart, 0, NULL, 0, NULL, 0);
}

static void erase_storage(void)
{
	ui_submit(LocalRequestKind_ResetStorage, 0, NULL, 0, NULL, 0);
}

static void confirm_restart(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_dialog_show(&oskey_refresh, "Restart OSKey?",
		       "The device will disconnect briefly. Stored data will not change.",
		       "Restart", UI_TONE_ACTIVE, restart_device);
}

static void confirm_reset(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_dialog_show(&oskey_trash, "Erase device data?",
		       "Wallet, passkeys and network settings will be permanently removed.",
		       "Erase data", UI_TONE_DANGER, erase_storage);
}

static void show_splash(void)
{
	lv_obj_t *content = ui_page_begin("", UI_NAVIGATION_NONE);
	lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	ui_icon_color(ui_icon(content, &oskey_wallet_logo), ui_tone_color(UI_TONE_ACTIVE));
	lv_obj_t *name = lv_label_create(content);
	lv_obj_set_width(name, LV_PCT(100));
	lv_obj_set_style_text_color(name, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(name, UI_FONT_LARGE, 0);
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
		bool enabled = ui.features[i];
		ui_list_row(content, enabled ? &oskey_success : &oskey_failure, names[i], NULL,
			    NULL, enabled ? UI_TONE_SUCCESS : UI_TONE_MUTED, NULL, NULL);
	}
	if (ui.status.wallet != WalletState_Disabled) {
		ui_section(content, "SETUP");
		ui_list_row(content, &oskey_wallet, "Set up OSKey", "Create or restore a wallet",
			    NULL, UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_PIN_NEW);
	}
}

static void show_home(void)
{
	lv_obj_t *content = ui_page_begin("OSKey", UI_NAVIGATION_NONE);
	ui_clear_sensitive();

	ui_list_row(content, &oskey_wallet, "Hardware wallet", "USB, Bluetooth or UART", NULL,
		    UI_TONE_ACTIVE, NULL, NULL);
#if defined(CONFIG_OSKEY_FIDO2)
	ui_list_row(content, &oskey_passkey, "Passkeys", "FIDO2 over USB", NULL, UI_TONE_ACTIVE,
		    NULL, NULL);
#endif
	ui_list_row(content, &oskey_settings, "Device settings", NULL, NULL, UI_TONE_ACTIVE,
		    navigate, (void *)(uintptr_t)UI_PAGE_SETTINGS);
}

static void show_settings(void)
{
	lv_obj_t *content = ui_page_begin("Device settings", UI_NAVIGATION_BACK);
	ui_clear_sensitive();
#if defined(CONFIG_OSKEY_WIFI) || defined(CONFIG_OSKEY_BLUETOOTH) || defined(CONFIG_OSKEY_USB)
	ui_section(content, "CONNECTIVITY");
#endif
#if defined(CONFIG_OSKEY_WIFI)
	ui_list_row(content, &oskey_wifi, "Wi-Fi", "Station, access point and saved network", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_WIFI);
#endif
#if defined(CONFIG_OSKEY_BLUETOOTH)
	ui_list_row(content, &oskey_bluetooth, "Bluetooth", "Wireless connection status", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_BLUETOOTH);
#endif
#if defined(CONFIG_OSKEY_USB)
	ui_list_row(content, &oskey_usb, "USB", "Host connection and available interfaces", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_USB);
#endif
	ui_section(content, "MAINTENANCE");
	ui_list_row(content, &oskey_refresh, "Restart", "Restart without changing data", NULL,
		    UI_TONE_ACTIVE, confirm_restart, NULL);
	ui_list_row(content, &oskey_trash, "Erase wallet", "Remove wallet data permanently", NULL,
		    UI_TONE_DANGER, confirm_reset, NULL);
}

static void show_source(void)
{
	lv_obj_t *content = ui_page_begin("Create wallet", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_document, "Choose a recovery source",
		    "Generate a new phrase or restore one", NULL, UI_TONE_ACTIVE, NULL, NULL);
	ui_section(content, "RECOVERY SOURCE");
	ui_list_row(content, &oskey_wallet, "Generate recovery phrase",
		    ui.features[APP_FEATURE_HARDWARE_RNG] ? "Create with hardware randomness"
							  : "Test-only without hardware RNG",
		    NULL, UI_TONE_ACTIVE, generate_mnemonic, NULL);
	ui_list_row(content, &oskey_document, "Import recovery phrase",
		    "Restore an existing wallet", NULL, UI_TONE_ACTIVE, navigate,
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
		    NULL, UI_TONE_ACTIVE, NULL, NULL);
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
	ui_list_row(content, &oskey_shuffle, "Enter custom entropy", NULL, NULL, UI_TONE_ACTIVE,
		    enable_custom_entropy, NULL);
}

static void mnemonic_saved(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_push(UI_PAGE_VERIFY);
}

static void wipe_label(lv_event_t *event)
{
	const char *text = lv_label_get_text(lv_event_get_target_obj(event));

	if (text != NULL) {
		ui_wipe((void *)text, strlen(text));
	}
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
		lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
		lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(label, lv_color_hex(0x242b33), 0);
		lv_obj_set_style_border_width(label, 1, 0);
		lv_obj_set_style_pad_ver(label, 5, 0);
		lv_label_set_text(label, row);
		lv_obj_add_event_cb(label, wipe_label, LV_EVENT_DELETE, NULL);
		ui_wipe(row, sizeof(row));
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
	ui_submit(LocalRequestKind_GenerateMnemonic, len / 4 * 3, entropy, len, NULL, 0);
	ui_wipe(entropy, sizeof(entropy));
}

static void show_entropy(void)
{
	uint8_t columns = entropy_columns();
	lv_obj_t *content = ui_page_begin("Custom entropy", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_shuffle, "Set each entropy bit", "Tap a bit to toggle 0 or 1",
		    NULL, UI_TONE_ACTIVE, NULL, NULL);
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
	lv_obj_set_style_text_font(table, UI_FONT_BODY, LV_PART_ITEMS);
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
		    UI_TONE_ACTIVE, confirm_restart, NULL);
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
	case UI_PAGE_PIN_NEW:
	case UI_PAGE_PIN_CONFIRM:
#if defined(CONFIG_OSKEY_FIDO2)
	case UI_PAGE_FIDO_PIN_RECOVER:
#endif
	case UI_PAGE_IMPORT:
	case UI_PAGE_VERIFY:
	case UI_PAGE_PASSPHRASE:
	case UI_PAGE_PASSPHRASE_CONFIRM:
		ui_input_page(&input_pages[ui.page], submit_current_input);
		break;
	case UI_PAGE_HOME:
		show_home();
		break;
	case UI_PAGE_CAMERA:
		ui_camera_render();
		break;
#if defined(CONFIG_OSKEY_QR_SCANNER)
	case UI_PAGE_QR_SCANNER:
		ui_qr_render();
		break;
#endif
	case UI_PAGE_SETTINGS:
		show_settings();
		break;
	case UI_PAGE_WIFI:
		ui_wifi_render();
		break;
	case UI_PAGE_WIFI_PASSWORD:
		ui_wifi_password_render();
		break;
	case UI_PAGE_BLUETOOTH:
		ui_bluetooth_render();
		break;
	case UI_PAGE_USB:
		ui_usb_render();
		break;
#if !defined(CONFIG_OSKEY_FIDO2)
	case UI_PAGE_FIDO_PIN_RECOVER:
		break;
#endif
	case UI_PAGE_SOURCE:
		show_source();
		break;
	case UI_PAGE_LENGTH:
		show_length();
		break;
	case UI_PAGE_MNEMONIC:
		show_mnemonic();
		break;
	case UI_PAGE_ENTROPY:
		show_entropy();
		break;
	case UI_PAGE_STORAGE_ERROR:
		show_storage_error();
		break;
	case UI_PAGE_CONFIRMATION:
		if (!ui_render_confirmation()) {
			ui.confirmation_id = 0;
			ui_back();
			return;
		}
		break;
	case UI_PAGE_NONE:
		break;
	}
	lv_obj_update_layout(ui.content);
	lv_obj_scroll_to(ui.content, 0, 0, LV_ANIM_OFF);
}

void ui_show_startup(void)
{
	if (ui.status.storage == APP_STORAGE_ERROR) {
		ui_open(UI_PAGE_STORAGE_ERROR);
		return;
	}

	switch (ui.status.wallet) {
	case WalletState_Locked:
		ui_open(UI_PAGE_LOCKED);
		break;
	case WalletState_Ready:
		ui_open(UI_PAGE_HOME);
		break;
	case WalletState_Setup:
	case WalletState_Busy:
	default:
		ui_open(UI_PAGE_CAPABILITIES);
		break;
	}
}
