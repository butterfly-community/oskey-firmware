// SPDX-License-Identifier: Apache-2.0

#include "wifi_portal.h"

#include <errno.h>
#include <string.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/wifi_mgmt.h>

#define WIFI_PORTAL_REQUEST_SIZE 384

static const char portal_page[] =
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>OSKey Wi-Fi</title><style>"
	"body{font-family:sans-serif;max-width:28rem;margin:3rem auto;padding:0 1rem}"
	"label,input,button{display:block;width:100%;box-sizing:border-box}"
	"label{margin-top:1rem}input,button{font-size:1rem;padding:.7rem}"
	"button{margin-top:1.5rem}</style></head><body>"
	"<h1>Configure Wi-Fi</h1><form method=\"post\" action=\"/configure\">"
	"<label>Wi-Fi name (SSID)</label><input name=\"ssid\" maxlength=\"32\" required>"
	"<label>Password</label><input name=\"password\" type=\"password\" maxlength=\"63\">"
	"<button type=\"submit\">Connect</button></form></body></html>";

static const char accepted_page[] =
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>OSKey Wi-Fi</title></head><body><h1>Connecting</h1>"
	"<p>The setup access point will now close.</p></body></html>";

static const char invalid_page[] =
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>OSKey Wi-Fi</title></head><body><h1>Invalid Wi-Fi settings</h1>"
	"<p>SSID must be 1-32 bytes. Password must be empty for an open network or 8-63 bytes.</p>"
	"<p><a href=\"/\">Try again</a></p></body></html>";

static char request_body[WIFI_PORTAL_REQUEST_SIZE];
static size_t request_body_len;
static char submitted_ssid[WIFI_SSID_MAX_LEN + 1];
static char submitted_password[WIFI_PSK_MAX_LEN + 1];
static size_t submitted_ssid_len;
static size_t submitted_password_len;
static bool submission_ready;
static wifi_portal_submit_cb_t portal_submit_cb;

static int hex_value(char value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}
	if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	}
	if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}

	return -EINVAL;
}

static int url_decode(char *output, size_t output_size, const char *input, size_t input_len)
{
	size_t output_len = 0;

	for (size_t i = 0; i < input_len; i++) {
		char value = input[i];

		if (value == '+') {
			value = ' ';
		} else if (value == '%') {
			if (i + 2 >= input_len) {
				return -EINVAL;
			}

			int high = hex_value(input[++i]);
			int low = hex_value(input[++i]);

			if (high < 0 || low < 0) {
				return -EINVAL;
			}

			value = (char)((high << 4) | low);
			if (value == '\0') {
				return -EINVAL;
			}
		}

		if (output_len + 1 >= output_size) {
			return -ENOSPC;
		}

		output[output_len++] = value;
	}

	output[output_len] = '\0';
	return output_len;
}

static int decode_field(const char *key, const char *value, size_t value_len)
{
	if (strcmp(key, "ssid") == 0) {
		int ret = url_decode(submitted_ssid, sizeof(submitted_ssid), value, value_len);

		if (ret < 1 || ret > WIFI_SSID_MAX_LEN) {
			return -EINVAL;
		}

		submitted_ssid_len = ret;
		return 0;
	}

	if (strcmp(key, "password") == 0) {
		int ret = url_decode(submitted_password, sizeof(submitted_password), value,
				     value_len);

		if (ret < 0 || (ret > 0 && ret < 8) || ret > 63) {
			return -EINVAL;
		}

		submitted_password_len = ret;
	}

	return 0;
}

static int parse_request_body(void)
{
	char *cursor = request_body;
	char *end = request_body + request_body_len;
	bool found_ssid = false;
	bool found_password = false;

	submitted_ssid_len = 0;
	submitted_password_len = 0;

	while (cursor < end) {
		char *separator = memchr(cursor, '&', end - cursor);
		char *field_end = separator ? separator : end;
		char *equals = memchr(cursor, '=', field_end - cursor);

		if (equals == NULL) {
			return -EINVAL;
		}

		*equals = '\0';
		if (strcmp(cursor, "ssid") == 0) {
			found_ssid = true;
		} else if (strcmp(cursor, "password") == 0) {
			found_password = true;
		}

		int ret = decode_field(cursor, equals + 1, field_end - equals - 1);

		if (ret < 0) {
			return ret;
		}

		cursor = field_end + (separator != NULL);
	}

	return found_ssid && found_password && submitted_ssid_len > 0 ? 0 : -EINVAL;
}

static int configure_handler(struct http_client_ctx *client, enum http_transaction_status status,
			     const struct http_request_ctx *request_ctx,
			     struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		memset(request_body, 0, sizeof(request_body));
		memset(submitted_password, 0, sizeof(submitted_password));
		request_body_len = 0;
		submission_ready = false;
		return 0;
	}

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		if (submission_ready && portal_submit_cb != NULL) {
			submission_ready = false;
			portal_submit_cb(submitted_ssid, submitted_ssid_len, submitted_password,
					 submitted_password_len);
		}
		memset(request_body, 0, sizeof(request_body));
		memset(submitted_password, 0, sizeof(submitted_password));
		request_body_len = 0;
		return 0;
	}

	if (request_ctx->data_len > sizeof(request_body) - request_body_len) {
		request_body_len = 0;
		return -ENOMEM;
	}

	if (request_ctx->data_len > 0) {
		memcpy(request_body + request_body_len, request_ctx->data, request_ctx->data_len);
	}
	request_body_len += request_ctx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		if (parse_request_body() == 0) {
			response_ctx->status = HTTP_200_OK;
			response_ctx->body = (const uint8_t *)accepted_page;
			response_ctx->body_len = sizeof(accepted_page) - 1;
			submission_ready = true;
		} else {
			response_ctx->status = HTTP_400_BAD_REQUEST;
			response_ctx->body = (const uint8_t *)invalid_page;
			response_ctx->body_len = sizeof(invalid_page) - 1;
			submission_ready = false;
		}
		response_ctx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_static portal_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/html",
	},
	.static_data = portal_page,
	.static_data_len = sizeof(portal_page) - 1,
};

static struct http_resource_detail_dynamic configure_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		.content_type = "text/html",
	},
	.cb = configure_handler,
};

static uint16_t portal_port = 80;

HTTP_SERVICE_DEFINE(wifi_portal_service, NULL, &portal_port, 1, 1, NULL,
		    &portal_resource_detail.common, NULL);
HTTP_RESOURCE_DEFINE(wifi_portal_root, wifi_portal_service, "/", &portal_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_configure, wifi_portal_service, "/configure",
		     &configure_resource_detail);

void wifi_portal_init(wifi_portal_submit_cb_t submit_cb)
{
	portal_submit_cb = submit_cb;
}

int wifi_portal_start(void)
{
	int ret = http_server_start();

	return ret == -EALREADY ? 0 : ret;
}

int wifi_portal_stop(void)
{
	int ret = http_server_stop();

	return ret == -EALREADY ? 0 : ret;
}
