// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/sys/atomic.h>

#include "bus.h"

LOG_MODULE_REGISTER(http);

#define PUBLIC_IP_HOST "checkip.amazonaws.com"

struct public_ip_response {
	char address[APP_PUBLIC_IP_MAX_LEN + 1];
	size_t length;
	int status;
};

static atomic_t wifi_connected;

static void public_ip_publish(const char *address)
{
	struct app_network_event event = {
		.kind = APP_NETWORK_EVENT_PUBLIC_IP,
	};

	if (address != NULL) {
		snprintk(event.data.public_ip.address, sizeof(event.data.public_ip.address), "%s",
			 address);
	}
	int ret = zbus_chan_pub(&app_network_event_chan, &event, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish public IP: %d", ret);
	}
}

static void public_ip_clear_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	public_ip_publish(NULL);
}

static K_WORK_DEFINE(public_ip_clear, public_ip_clear_handler);

static int public_ip_response_cb(struct http_response *response, enum http_final_call final_data,
				 void *user_data)
{
	ARG_UNUSED(final_data);
	struct public_ip_response *result = user_data;

	if (response->http_status_code != 0) {
		result->status = response->http_status_code;
	}
	if (result->status == 200 && response->body_frag_start != NULL &&
	    result->length < sizeof(result->address) - 1) {
		size_t available = sizeof(result->address) - 1 - result->length;
		size_t length = MIN(response->body_frag_len, available);

		memcpy(&result->address[result->length], response->body_frag_start, length);
		result->length += length;
		result->address[result->length] = '\0';
	}

	return 0;
}

static void fetch_public_ip(void)
{
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *address;
	uint8_t receive_buffer[256];
	struct public_ip_response response = {0};
	int ret = getaddrinfo(PUBLIC_IP_HOST, "80", &hints, &address);

	if (ret != 0) {
		LOG_WRN("Cannot resolve %s: %d", PUBLIC_IP_HOST, ret);
		return;
	}

	int sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

	if (sock < 0) {
		LOG_WRN("Cannot create HTTP socket: %d", errno);
		goto free_address;
	}

	if (connect(sock, address->ai_addr, address->ai_addrlen) < 0) {
		LOG_WRN("Cannot connect to %s: %d", PUBLIC_IP_HOST, errno);
		goto close_socket;
	}

	struct http_request request = {
		.method = HTTP_GET,
		.url = "/",
		.host = PUBLIC_IP_HOST,
		.protocol = "HTTP/1.1",
		.response = public_ip_response_cb,
		.recv_buf = receive_buffer,
		.recv_buf_len = sizeof(receive_buffer),
	};

	ret = http_client_req(sock, &request, 5 * MSEC_PER_SEC, &response);
	if (ret < 0) {
		LOG_WRN("Public IP request failed: %d", ret);
	} else if (response.status != 200) {
		LOG_WRN("Public IP request returned HTTP %d", response.status);
	} else {
		while (response.length > 0 &&
		       isspace((unsigned char)response.address[response.length - 1])) {
			response.address[--response.length] = '\0';
		}
		if (response.length > 0 && atomic_get(&wifi_connected) != 0) {
			LOG_INF("Public IP: %s", response.address);
			public_ip_publish(response.address);
		}
	}

close_socket:
	(void)close(sock);
free_address:
	freeaddrinfo(address);
}

static void public_ip_check_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (atomic_get(&wifi_connected) != 0) {
		fetch_public_ip();
	}
}

static K_WORK_DELAYABLE_DEFINE(public_ip_check, public_ip_check_handler);

static void wifi_state_changed(const struct zbus_channel *channel)
{
	const struct app_network_event *event = zbus_chan_const_msg(channel);

	if (event->kind != APP_NETWORK_EVENT_WIFI_STATE) {
		return;
	}
	const struct app_wifi_state *wifi = &event->data.wifi;
	bool connected = wifi->sta == APP_WIFI_STA_CONNECTED;

	if (connected && atomic_cas(&wifi_connected, 0, 1)) {
		k_work_submit(&public_ip_clear);
		k_work_reschedule(&public_ip_check, K_SECONDS(30));
	} else if (!connected && atomic_cas(&wifi_connected, 1, 0)) {
		k_work_cancel_delayable(&public_ip_check);
		k_work_submit(&public_ip_clear);
	}
}

ZBUS_LISTENER_DEFINE(http_wifi_listener, wifi_state_changed);
ZBUS_CHAN_ADD_OBS(app_network_event_chan, http_wifi_listener, 0);
