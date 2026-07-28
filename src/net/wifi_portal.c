// SPDX-License-Identifier: Apache-2.0

#include "wifi_portal.h"

#include <errno.h>
#include <string.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/wifi_mgmt.h>

#define WIFI_PORTAL_REQUEST_SIZE (WIFI_SSID_MAX_LEN + 1 + 63)

static const char portal_page[] =
	"<!doctype html><html><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>OSKey Wi-Fi</title><style>"
	"body{font-family:sans-serif;max-width:28rem;margin:3rem auto;padding:0 1rem}"
	"label,input,button{display:block;width:100%;box-sizing:border-box}"
	"label{margin-top:1rem}input,button{font-size:1rem;padding:.7rem}"
	"button{margin-top:1.5rem}</style></head><body>"
	"<h1>Configure Wi-Fi</h1><form "
	"onsubmit=\"event.preventDefault();fetch('/configure',{method:'POST',"
	"body:this.ssid.value+':'+this.password.value}).then(r=>r.text()).then(t=>document.body."
	"innerHTML=t)\">"
	"<label>Wi-Fi name (SSID)</label><input name=\"ssid\" maxlength=\"32\" pattern=\"[^:]*\" "
	"required>"
	"<label>Password</label><input name=\"password\" type=\"password\" maxlength=\"63\">"
	"<button type=\"submit\">Connect</button></form></body></html>";

static const char accepted_page[] =
	"<h1>Connecting</h1><p>The setup access point will now close.</p>";

static const char invalid_page[] =
	"<h1>Invalid Wi-Fi settings</h1>"
	"<p>SSID must be 1-32 bytes. Password must be empty for an open network or 8-63 bytes.</p>"
	"<p><a href=\"/\">Try again</a></p>";

static char request_body[WIFI_PORTAL_REQUEST_SIZE];
static size_t request_body_len;
static wifi_portal_submit_cb_t portal_submit_cb;

static void reset_request(void)
{
	memset(request_body, 0, sizeof(request_body));
	request_body_len = 0;
}

static char *parse_request_body(void)
{
	char *password = memchr(request_body, ':', request_body_len);

	if (password == NULL) {
		return NULL;
	}

	size_t ssid_len = password - request_body;
	size_t password_len = request_body_len - ssid_len - 1;

	if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN ||
	    (password_len > 0 && password_len < 8) || password_len > 63) {
		return NULL;
	}

	return password;
}

static int configure_handler(struct http_client_ctx *client, enum http_transaction_status status,
			     const struct http_request_ctx *request_ctx,
			     struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		reset_request();
		return 0;
	}

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		char *password = parse_request_body();

		if (password != NULL && portal_submit_cb != NULL) {
			size_t ssid_len = password - request_body;

			portal_submit_cb(request_body, ssid_len, password + 1,
					 request_body_len - ssid_len - 1);
		}
		reset_request();
		return 0;
	}

	if (request_ctx->data_len > sizeof(request_body) - request_body_len) {
		reset_request();
		return -ENOMEM;
	}

	if (request_ctx->data_len > 0) {
		memcpy(request_body + request_body_len, request_ctx->data, request_ctx->data_len);
	}
	request_body_len += request_ctx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		if (parse_request_body() != NULL) {
			response_ctx->status = HTTP_200_OK;
			response_ctx->body = (const uint8_t *)accepted_page;
			response_ctx->body_len = sizeof(accepted_page) - 1;
		} else {
			response_ctx->status = HTTP_400_BAD_REQUEST;
			response_ctx->body = (const uint8_t *)invalid_page;
			response_ctx->body_len = sizeof(invalid_page) - 1;
			reset_request();
		}
		response_ctx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_static portal_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/html",
		},
	.static_data = portal_page,
	.static_data_len = sizeof(portal_page) - 1,
};

static struct http_resource_detail_dynamic configure_resource_detail = {
	.common =
		{
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
