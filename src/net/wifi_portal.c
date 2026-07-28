// SPDX-License-Identifier: Apache-2.0

#include "wifi_portal.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#define WIFI_PORTAL_REQUEST_SIZE (WIFI_SSID_MAX_LEN + 1 + 63)

static const uint8_t portal_page[] = {
#include "wifi_portal.html.gz.inc"
};

static char request_body[WIFI_PORTAL_REQUEST_SIZE];
static size_t request_body_len;
static char *wifi_password;
static wifi_portal_submit_cb_t portal_submit_cb;

struct post_endpoint {
	enum http_status (*handle)(enum http_transaction_status status, char *body, size_t len);
};

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static int hostname_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				 void *cb_arg)
{
	char hostname[NET_HOSTNAME_MAX_LEN];

	if (strcmp(name, "hostname") != 0) {
		return -ENOENT;
	}

	if (len == 0 || len > sizeof(hostname)) {
		return -EINVAL;
	}

	ssize_t ret = read_cb(cb_arg, hostname, len);

	return ret < 0 ? ret : net_hostname_set(hostname, ret);
}

SETTINGS_STATIC_HANDLER_DEFINE(wifi_portal, "oskey", NULL, hostname_settings_set, NULL, NULL);

static enum http_status wifi_post(enum http_transaction_status status, char *body, size_t len)
{
	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		wifi_password = NULL;
		return HTTP_200_OK;
	}

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE && wifi_password != NULL &&
	    portal_submit_cb != NULL) {
		size_t ssid_len = wifi_password - body;

		portal_submit_cb(body, ssid_len, wifi_password + 1, len - ssid_len - 1);
	}

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		wifi_password = NULL;
		return HTTP_200_OK;
	}

	wifi_password = memchr(body, ':', len);
	if (wifi_password == NULL) {
		return HTTP_400_BAD_REQUEST;
	}

	size_t ssid_len = wifi_password - body;
	size_t password_len = len - ssid_len - 1;

	if (ssid_len == 0 || ssid_len > WIFI_SSID_MAX_LEN ||
	    (password_len > 0 && password_len < 8) || password_len > 63) {
		wifi_password = NULL;
		return HTTP_400_BAD_REQUEST;
	}

	return HTTP_200_OK;
}

static enum http_status hostname_post(enum http_transaction_status status, char *body, size_t len)
{
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return HTTP_200_OK;
	}

	if (len == 0 || len > NET_HOSTNAME_MAX_LEN) {
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
		endpoint->handle(status, request_body, request_body_len);
		request_body_len = 0;
		return 0;
	}

	if (request_ctx->data_len > sizeof(request_body) - request_body_len) {
		endpoint->handle(HTTP_SERVER_TRANSACTION_ABORTED, request_body, request_body_len);
		request_body_len = 0;
		return -ENOMEM;
	}

	if (request_ctx->data_len > 0) {
		memcpy(request_body + request_body_len, request_ctx->data, request_ctx->data_len);
	}
	request_body_len += request_ctx->data_len;

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	response_ctx->status = endpoint->handle(status, request_body, request_body_len);
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
HTTP_RESOURCE_DEFINE(wifi_portal_configure, wifi_portal_service, "/configure",
		     &configure_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_hostname, wifi_portal_service, "/hostname",
		     &hostname_resource_detail);
HTTP_RESOURCE_DEFINE(wifi_portal_reboot, wifi_portal_service, "/reboot", &reboot_resource_detail);

void wifi_portal_init(wifi_portal_submit_cb_t submit_cb)
{
	portal_submit_cb = submit_cb;
}

int wifi_portal_start(void)
{
	int ret = http_server_start();

	return ret == -EALREADY ? 0 : ret;
}
