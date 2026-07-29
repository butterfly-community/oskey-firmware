// SPDX-License-Identifier: Apache-2.0

#include "http.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>

LOG_MODULE_REGISTER(http);

#define PUBLIC_IP_HOST "ifconfig.me"

static int public_ip_response_cb(struct http_response *response, enum http_final_call final_data,
				 void *user_data)
{
	ARG_UNUSED(final_data);
	ARG_UNUSED(user_data);

	if (response->http_status_code == 200 && response->body_frag_start != NULL) {
		LOG_INF("Public IP: %.*s", (int)response->body_frag_len,
			(char *)response->body_frag_start);
	}

	return 0;
}

static void log_public_ip(void)
{
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *address;
	uint8_t receive_buffer[256];
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
		.url = "/ip",
		.host = PUBLIC_IP_HOST,
		.protocol = "HTTP/1.1",
		.response = public_ip_response_cb,
		.recv_buf = receive_buffer,
		.recv_buf_len = sizeof(receive_buffer),
	};

	ret = http_client_req(sock, &request, 5 * MSEC_PER_SEC, NULL);
	if (ret < 0) {
		LOG_WRN("Public IP request failed: %d", ret);
	}

close_socket:
	(void)close(sock);
free_address:
	freeaddrinfo(address);
}

static void public_ip_check_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	log_public_ip();
}

static K_WORK_DELAYABLE_DEFINE(public_ip_check, public_ip_check_handler);

void http_public_ip_check_schedule(void)
{
	k_work_reschedule(&public_ip_check, K_SECONDS(30));
}

void http_public_ip_check_cancel(void)
{
	k_work_cancel_delayable(&public_ip_check);
}
