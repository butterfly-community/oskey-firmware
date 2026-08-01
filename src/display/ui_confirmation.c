#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"
#include "confirmation.h"

#define CONFIRMATION_HEX_BYTES 64

static void respond(lv_event_t *event)
{
	bool approved = (uintptr_t)lv_event_get_user_data(event);
	app_confirmation_respond(approved);
}

static void field(lv_obj_t *parent, const char *name, const uint8_t *value, size_t len,
		  enum ui_tone tone)
{
	if (value == NULL || len == 0) {
		return;
	}

	lv_obj_t *row = lv_obj_create(parent);
	lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(row, lv_color_hex(0x242b33), 0);
	lv_obj_set_style_border_width(row, 1, 0);
	lv_obj_set_style_radius(row, 0, 0);
	lv_obj_set_style_pad_hor(row, 4, 0);
	lv_obj_set_style_pad_ver(row, 6, 0);
	lv_obj_set_style_pad_row(row, 2, 0);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *name_label = lv_label_create(row);
	lv_obj_set_style_text_font(name_label, &lv_font_montserrat_10, 0);
	lv_obj_set_style_text_color(name_label, lv_color_hex(0x929eaa), 0);
	lv_label_set_text(name_label, name);

	lv_obj_t *value_label = lv_label_create(row);
	lv_obj_set_width(value_label, LV_PCT(100));
	lv_obj_set_style_text_font(value_label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_color(value_label,
				    tone == UI_TONE_WARNING ? ui_tone_color(UI_TONE_WARNING)
							    : ui_tone_color(UI_TONE_DEFAULT),
				    0);
	lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
	lv_label_set_text_fmt(value_label, "%.*s", (int)len, (const char *)value);
}

static void text_field(lv_obj_t *parent, const char *name, const char *value)
{
	field(parent, name, (const uint8_t *)value, strlen(value), UI_TONE_DEFAULT);
}

static void number(lv_obj_t *parent, const char *name, uint64_t value)
{
	char text[24];
	snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
	text_field(parent, name, text);
}

static void hex(lv_obj_t *parent, const char *name, struct AppSlice value)
{
	if (value.data == NULL || value.len == 0) {
		return;
	}

	static const char digits[] = "0123456789abcdef";
	size_t len = MIN(value.len, CONFIRMATION_HEX_BYTES);
	char text[2 + CONFIRMATION_HEX_BYTES * 2 + CONFIRMATION_HEX_BYTES / 4 + 4];
	size_t out = 0;
	text[out++] = '0';
	text[out++] = 'x';
	for (size_t i = 0; i < len; ++i) {
		if (i > 0 && i % 4 == 0) {
			text[out++] = ' ';
		}
		text[out++] = digits[value.data[i] >> 4];
		text[out++] = digits[value.data[i] & 0xf];
	}
	if (len < value.len) {
		memcpy(text + out, "...", 3);
		out += 3;
	}
	text[out] = '\0';
	text_field(parent, name, text);
}

static const char *fido_operation(FidoOperation operation)
{
	switch (operation) {
	case FidoOperation_Register:
		return "Create a passkey for this service";
	case FidoOperation_Authenticate:
		return "Authenticate with a passkey";
	case FidoOperation_Select:
		return "Confirm physical presence";
	case FidoOperation_Unspecified:
	default:
		return "Review authentication request";
	}
}

static void details_clicked(lv_event_t *event)
{
	lv_obj_t *details = lv_event_get_user_data(event);

	if (lv_obj_has_flag(details, LV_OBJ_FLAG_HIDDEN)) {
		lv_obj_clear_flag(details, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(details, LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_update_layout(ui.content);
}

static lv_obj_t *technical_details(lv_obj_t *content)
{
	lv_obj_t *details = lv_obj_create(content);
	lv_obj_set_size(details, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(details, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(details, 0, 0);
	lv_obj_set_style_radius(details, 0, 0);
	lv_obj_set_style_pad_all(details, 0, 0);
	lv_obj_set_style_pad_row(details, 0, 0);
	lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
	lv_obj_remove_flag(details, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					    LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(details, LV_OBJ_FLAG_HIDDEN);
	ui_list_row(content, NULL, "Technical details", "Hashes and protocol fields", NULL,
		    UI_TONE_DEFAULT, details_clicked, details);
	lv_obj_move_to_index(details, -1);
	return details;
}

void ui_show_confirmation(const struct AppConfirmationView *confirmation)
{
	if (confirmation->kind != AppConfirmationKind_EthMessage &&
	    confirmation->kind != AppConfirmationKind_EthTransaction &&
	    confirmation->kind != AppConfirmationKind_Fido) {
		app_confirmation_respond(false);
		return;
	}

	const char *title = "Confirm request";
	const char *approve = "Approve";
	const char *summary = "Review before approving";
	const void *icon = &oskey_ethereum;

	if (ui.confirmation_return == UI_PAGE_NONE) {
		ui.confirmation_return = ui.page;
	}

	if (confirmation->kind == AppConfirmationKind_EthMessage) {
		title = "Sign message";
		approve = "Sign";
		summary = "Ethereum message";
	} else if (confirmation->kind == AppConfirmationKind_EthTransaction) {
		title = "Sign transaction";
		approve = "Sign";
		summary = confirmation->contract_creation ? "Ethereum contract creation"
							  : "Ethereum transaction";
	} else if (confirmation->kind == AppConfirmationKind_Fido) {
		icon = &oskey_passkey;
		summary = fido_operation(confirmation->operation);
		switch (confirmation->operation) {
		case FidoOperation_Register:
			title = "Create passkey";
			break;
		case FidoOperation_Authenticate:
			title = "Use passkey";
			break;
		case FidoOperation_Select:
			title = "Confirm presence";
			break;
		case FidoOperation_Unspecified:
			break;
		}
	}

	lv_obj_t *content = ui_page_begin(title, UI_NAVIGATION_NONE);
	ui_list_row(content, icon, summary, NULL, NULL, UI_TONE_DEFAULT, NULL, NULL);

	switch (confirmation->kind) {
	case AppConfirmationKind_EthMessage:
		ui_section(content, "MESSAGE");
		field(content, "Preview", confirmation->preview.data, confirmation->preview.len,
		      UI_TONE_DEFAULT);
		if (confirmation->truncated) {
			static const uint8_t warning[] = "Only part of this message can be shown";
			field(content, "Warning", warning, sizeof(warning) - 1, UI_TONE_WARNING);
		}
		number(content, "Message size (bytes)", confirmation->message_length);
		lv_obj_t *message_details = technical_details(content);
		hex(message_details, "Signing hash", confirmation->signing_hash);
		break;
	case AppConfirmationKind_EthTransaction:
		ui_section(content, "OVERVIEW");
		number(content, "Chain ID", confirmation->chain_id);
		if (confirmation->contract_creation) {
			text_field(content, "To", "New contract");
		} else {
			hex(content, "To", confirmation->to);
		}
		field(content, "Value", confirmation->value.data, confirmation->value.len,
		      UI_TONE_DEFAULT);
		if (confirmation->input_length > 0) {
			number(content, "Contract data (bytes)", confirmation->input_length);
		}
		lv_obj_t *transaction_details = technical_details(content);
		number(transaction_details, "Nonce", confirmation->nonce);
		field(transaction_details, "Gas price", confirmation->gas_price.data,
		      confirmation->gas_price.len, UI_TONE_DEFAULT);
		number(transaction_details, "Gas limit", confirmation->gas_limit);
		if (confirmation->input_length > 0) {
			hex(transaction_details, "Method", confirmation->selector);
			hex(transaction_details, "Input hash", confirmation->input_hash);
		}
		hex(transaction_details, "Signing hash", confirmation->signing_hash);
		break;
	case AppConfirmationKind_Fido:
		ui_section(content, "REQUEST");
		field(content, "Service", confirmation->rp_id.data, confirmation->rp_id.len,
		      UI_TONE_DEFAULT);
		if (confirmation->account_is_text) {
			field(content, "Account", confirmation->account.data,
			      confirmation->account.len, UI_TONE_DEFAULT);
		} else {
			hex(content, "Account ID", confirmation->account);
		}
		break;
	}

	ui_section(content, "ACTION");
	ui_list_row(content, &oskey_success, approve,
		    confirmation->kind == AppConfirmationKind_Fido
			    ? "Approve this request"
			    : "Sign after reviewing all details",
		    NULL, UI_TONE_SUCCESS, respond, (void *)(uintptr_t)true);
	ui_list_row(content, &oskey_failure, "Reject", "Do not approve this request", NULL,
		    UI_TONE_DANGER, respond, NULL);
	lv_obj_update_layout(content);
	lv_obj_scroll_to(content, 0, 0, LV_ANIM_OFF);
}

void ui_dismiss_confirmation(void)
{
	if (ui.confirmation_return == UI_PAGE_NONE) {
		return;
	}
	ui.page = ui.confirmation_return;
	ui.confirmation_return = UI_PAGE_NONE;
	ui_render();
}
