// SPDX-License-Identifier: Apache-2.0

#include "wifi_portal.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#define WIFI_PORTAL_REQUEST_SIZE (WIFI_SSID_MAX_LEN + 1 + 63)

BUILD_ASSERT(CONFIG_HTTP_SERVER_MAX_CLIENTS == 1,
	     "The Wi-Fi portal uses one shared request buffer");

static const uint8_t portal_page[] = {
#include "wifi_portal.html.gz.inc"
};

static const uint8_t captive_portal_status[] =
	"{\"captive\":true,\"user-portal-url\":\"http://" CONFIG_OSKEY_WIFI_AP_IP_ADDRESS "/\"}";

static char request_body[WIFI_PORTAL_REQUEST_SIZE];
static size_t request_body_len;
static bool request_authorized;
static wifi_portal_submit_cb_t portal_submit_cb;

HTTP_SERVER_REGISTER_HEADER_CAPTURE(oskey_request_header, "X-OSKey-Request");

struct post_endpoint {
	enum http_status (*handle)(enum http_transaction_status status, char *body, size_t len);
};

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static void request_body_reset(void)
{
	volatile char *body = request_body;

	while (request_body_len > 0) {
		body[--request_body_len] = 0;
	}
}

static bool request_has_authorization(const struct http_request_ctx *request)
{
	if (request->headers_status != HTTP_HEADER_STATUS_OK) {
		return false;
	}

	for (size_t i = 0; i < request->header_count; ++i) {
		if (strcasecmp(request->headers[i].name, "X-OSKey-Request") == 0 &&
		    strcmp(request->headers[i].value, "1") == 0) {
			return true;
		}
	}
	return false;
}

static bool hostname_is_valid(const char *hostname, size_t len)
{
	if (len == 0 || len > NET_HOSTNAME_MAX_LEN || hostname[0] == '-' ||
	    hostname[len - 1] == '-') {
		return false;
	}

	for (size_t i = 0; i < len; ++i) {
		char c = hostname[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		      c == '-')) {
			return false;
		}
	}

	return true;
}

static int hostname_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				 void *cb_arg)
{
	char hostname[NET_HOSTNAME_MAX_LEN];

	if (strcmp(name, "hostname") != 0) {
		return -ENOENT;
	}

	if (len > sizeof(hostname)) {
		return -EINVAL;
	}

	ssize_t ret = read_cb(cb_arg, hostname, len);

	if (ret < 0) {
		return ret;
	}
	if (!hostname_is_valid(hostname, ret)) {
		return -EINVAL;
	}

	return net_hostname_set(hostname, ret);
}

SETTINGS_STATIC_HANDLER_DEFINE(wifi_portal, "oskey", NULL, hostname_settings_set, NULL, NULL);

static enum http_status wifi_post(enum http_transaction_status status, char *body, size_t len)
{
	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		return HTTP_200_OK;
	}

	char *password = memchr(body, ':', len);
	if (password == NULL) {
		return HTTP_400_BAD_REQUEST;
	}

	size_t ssid_len = password - body;
	size_t password_len = len - ssid_len - 1;

	if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN ||
	    (password_len > 0 && password_len < 8) || password_len > 63) {
		return HTTP_400_BAD_REQUEST;
	}

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL && portal_submit_cb != NULL) {
		int ret = portal_submit_cb(body, ssid_len, password + 1, password_len);

		if (ret < 0) {
			return ret == -EBUSY ? HTTP_409_CONFLICT : HTTP_500_INTERNAL_SERVER_ERROR;
		}
	}

	return HTTP_200_OK;
}

static enum http_status hostname_post(enum http_transaction_status status, char *body, size_t len)
{
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return HTTP_200_OK;
	}

	if (!hostname_is_valid(body, len)) {
		return HTTP_400_BAD_REQUEST;
	}

	int ret = settings_save_one("oskey/hostname", body, len);

	if (ret == 0) {
		ret = net_hostname_set(body, len);
	}

	return ret == 0 ? HTTP_200_OK : HTTP_500_INTERNAL_SERVER_ERROR;
}

static enum http_status reboot_post(enum http_transaction_status status, char *body, size_t len)
{
	ARG_UNUSED(body);
	ARG_UNUSED(len);

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		k_work_reschedule(&reboot_work, K_MSEC(100));
	}

	return HTTP_200_OK;
}

static struct post_endpoint wifi_endpoint = {.handle = wifi_post};
static struct post_endpoint hostname_endpoint = {.handle = hostname_post};
static struct post_endpoint reboot_endpoint = {.handle = reboot_post};

static int post_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	struct post_endpoint *endpoint = user_data;

	ARG_UNUSED(client);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		if (request_authorized) {
			endpoint->handle(status, request_body, request_body_len);
		}
		request_body_reset();
		request_authorized = false;
		return 0;
	}

	if (request_ctx->headers_status != HTTP_HEADER_STATUS_NONE) {
		request_authorized = request_has_authorization(request_ctx);
	}

	if (request_ctx->data_len > sizeof(request_body) - request_body_len) {
		endpoint->handle(HTTP_SERVER_TRANSACTION_ABORTED, request_body, request_body_len);
		request_body_reset();
		request_authorized = false;
		return -ENOMEM;
	}

	if (request_ctx->data_len > 0) {
		memcpy(request_body + request_body_len, request_ctx->data, request_ctx->data_len);
	}
	request_body_len += request_ctx->data_len;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	response_ctx->status = request_authorized
				       ? endpoint->handle(status, request_body, request_body_len)
				       : HTTP_403_FORBIDDEN;
	request_body_reset();
	response_ctx->final_chunk = true;

	return 0;
}

static struct http_resource_detail_static portal_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = portal_page,
	.static_data_len = sizeof(portal_page),
};

static struct http_resource_detail_static captive_portal_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/captive+json",
		},
	.static_data = captive_portal_status,
	.static_data_len = sizeof(captive_portal_status) - 1,
};

static struct http_resource_detail_dynamic configure_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = post_handler,
	.user_data = &wifi_endpoint,
};

static struct http_resource_detail_dynamic hostname_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = post_handler,
	.user_data = &hostname_endpoint,
};

static struct http_resource_detail_dynamic reboot_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = post_handler,
	.user_data = &reboot_endpoint,
};

static uint16_t portal_port = 80;

HTTP_SERVICE_DEFINE(wifi_portal_service, NULL, &portal_port, 1, 1, NULL,
		    &portal_resource_detail.common, NULL);
HTTP_RESOURCE_DEFINE(wifi_portal_root, wifi_portal_service, "/", &portal_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_status, wifi_portal_service, "/generate_204",
		     &captive_portal_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_configure, wifi_portal_service, "/configure",
		     &configure_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_hostname, wifi_portal_service, "/hostname",
		     &hostname_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_reboot, wifi_portal_service, "/reboot", &reboot_resource_detail);

int wifi_portal_init(wifi_portal_submit_cb_t submit_cb)
{
	request_body_reset();
	request_authorized = false;
	portal_submit_cb = submit_cb;
	return http_server_start();
}
