// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2024 Muhammad Haziq
 */

#include "wifi.h"

#include <errno.h>
#include <string.h>

#if defined(CONFIG_WIFI) && defined(CONFIG_WIFI_USAGE_MODE_STA_AP)

#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>

#include "wifi_portal.h"

#include <zephyr/net/dhcpv4_server.h>

LOG_MODULE_REGISTER(wifi);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define PUBLIC_IP_HOST "ifconfig.me"

#define NET_EVENT_WIFI_MASK                                                                        \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |                      \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

enum app_wifi_state {
	APP_WIFI_IDLE,
	APP_WIFI_AP_STARTING,
	APP_WIFI_AP_ACTIVE,
	APP_WIFI_STA_CONNECTING,
	APP_WIFI_STA_DISCONNECTING,
	APP_WIFI_STA_RESTORE_PENDING,
	APP_WIFI_AP_STOPPING,
	APP_WIFI_STA_ACTIVE,
};

static struct net_if *sta_iface;
static struct net_mgmt_event_callback wifi_event_cb;
static enum app_wifi_state wifi_state;
static char sta_ssid[WIFI_SSID_MAX_LEN + 1];
static char sta_password[WIFI_PSK_MAX_LEN + 1];
static size_t sta_ssid_len;
static size_t sta_password_len;

static void wifi_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wifi_work, wifi_work_handler);

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

static void clear_sta_credentials(void)
{
	memset(sta_ssid, 0, sizeof(sta_ssid));
	memset(sta_password, 0, sizeof(sta_password));
	sta_ssid_len = 0;
	sta_password_len = 0;
}

static int save_sta_credentials(void)
{
	enum wifi_security_type security =
		sta_password_len == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	int ret = wifi_credentials_delete_all();

	if (ret < 0) {
		LOG_ERR("Failed to replace stored Wi-Fi credentials: %d", ret);
		return ret;
	}

	ret = wifi_credentials_set_personal(sta_ssid, sta_ssid_len, security, NULL, 0, sta_password,
					    sta_password_len, WIFI_CREDENTIALS_FLAG_2_4GHz,
					    WIFI_CHANNEL_ANY, 0);
	if (ret < 0) {
		LOG_ERR("Failed to store Wi-Fi credentials: %d", ret);
	}

	return ret;
}

static struct net_if *ap_iface;
static struct net_in_addr ap_addr;
static bool ap_addr_configured;
static bool dhcp_server_started;

BUILD_ASSERT(sizeof(CONFIG_OSKEY_WIFI_AP_SSID) > 1, "OSKey Wi-Fi AP SSID is empty");
BUILD_ASSERT(sizeof(CONFIG_OSKEY_WIFI_AP_SSID) - 1 <= WIFI_SSID_MAX_LEN,
	     "OSKey Wi-Fi AP SSID is too long");
BUILD_ASSERT(sizeof(CONFIG_OSKEY_WIFI_AP_PSK) == 1 ||
		     (sizeof(CONFIG_OSKEY_WIFI_AP_PSK) >= 9 &&
		      sizeof(CONFIG_OSKEY_WIFI_AP_PSK) <= WIFI_PSK_MAX_LEN),
	     "OSKey Wi-Fi AP password must be empty or 8-63 bytes");

static void stop_ap_network(void)
{
	if (dhcp_server_started) {
		int ret = net_dhcpv4_server_stop(ap_iface);

		if (ret < 0 && ret != -ENOENT) {
			LOG_WRN("Failed to stop AP DHCP server: %d", ret);
		}
		dhcp_server_started = false;
	}

	if (ap_addr_configured) {
		(void)net_if_ipv4_addr_rm(ap_iface, &ap_addr);
		ap_addr_configured = false;
	}
}

static int start_ap_network(void)
{
	struct net_in_addr netmask;
	struct net_in_addr pool_start;

	if (inet_pton(AF_INET, CONFIG_OSKEY_WIFI_AP_IP_ADDRESS, &ap_addr) != 1) {
		LOG_ERR("Invalid AP address: %s", CONFIG_OSKEY_WIFI_AP_IP_ADDRESS);
		return -EINVAL;
	}

	if (inet_pton(AF_INET, CONFIG_OSKEY_WIFI_AP_NETMASK, &netmask) != 1) {
		LOG_ERR("Invalid AP netmask: %s", CONFIG_OSKEY_WIFI_AP_NETMASK);
		return -EINVAL;
	}

	net_if_ipv4_set_gw(ap_iface, &ap_addr);

	if (net_if_ipv4_addr_add(ap_iface, &ap_addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_ERR("Failed to set AP IPv4 address");
		return -EIO;
	}
	ap_addr_configured = true;

	if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &ap_addr, &netmask)) {
		LOG_ERR("Failed to set AP netmask");
		stop_ap_network();
		return -EIO;
	}

	net_ipaddr_copy(&pool_start, &ap_addr);
	pool_start.s4_addr[3] += 10;

	int ret = net_dhcpv4_server_start(ap_iface, &pool_start);

	if (ret < 0) {
		LOG_ERR("Failed to start AP DHCP server: %d", ret);
		stop_ap_network();
		return ret;
	}

	dhcp_server_started = true;
	return 0;
}

static int enable_ap_mode(void)
{
	struct wifi_connect_req_params config = {0};

	if (ap_iface == NULL) {
		LOG_ERR("AP interface is not initialized");
		return -ENODEV;
	}

	int ret = start_ap_network();

	if (ret < 0) {
		return ret;
	}

	config.ssid = (const uint8_t *)CONFIG_OSKEY_WIFI_AP_SSID;
	config.ssid_length = sizeof(CONFIG_OSKEY_WIFI_AP_SSID) - 1;
	config.psk = (const uint8_t *)CONFIG_OSKEY_WIFI_AP_PSK;
	config.psk_length = sizeof(CONFIG_OSKEY_WIFI_AP_PSK) - 1;
	config.security = config.psk_length == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	config.channel = WIFI_CHANNEL_ANY;
	config.band = WIFI_FREQ_BAND_2_4_GHZ;

	wifi_state = APP_WIFI_AP_STARTING;
	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &config, sizeof(config));
	if (ret < 0) {
		LOG_ERR("Failed to enable AP mode: %d", ret);
		wifi_state = APP_WIFI_IDLE;
		stop_ap_network();
	}

	return ret;
}

static void provisioning_submitted(const char *ssid, size_t ssid_len, const char *password,
				   size_t password_len)
{
	memcpy(sta_ssid, ssid, ssid_len);
	sta_ssid[ssid_len] = '\0';
	sta_ssid_len = ssid_len;

	memcpy(sta_password, password, password_len);
	sta_password[password_len] = '\0';
	sta_password_len = password_len;

	k_work_reschedule(&wifi_work, K_NO_WAIT);
}

static void disable_ap_mode(void)
{
	wifi_state = APP_WIFI_AP_STOPPING;

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);

	if (ret < 0) {
		LOG_ERR("Failed to disable AP mode: %d", ret);
		wifi_state = APP_WIFI_STA_ACTIVE;
	}
}

static int connect_to_wifi(void)
{
	struct wifi_connect_req_params config = {0};

	if (sta_iface == NULL) {
		LOG_ERR("STA interface is not initialized");
		return -ENODEV;
	}

	if (sta_ssid_len == 0) {
		LOG_ERR("STA SSID is not configured");
		return -EINVAL;
	}

	config.ssid = (const uint8_t *)sta_ssid;
	config.ssid_length = sta_ssid_len;
	config.psk = (const uint8_t *)sta_password;
	config.psk_length = sta_password_len;
	config.security = sta_password_len == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	config.channel = WIFI_CHANNEL_ANY;
	config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s", sta_ssid);
	wifi_state = APP_WIFI_STA_CONNECTING;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &config, sizeof(config));

	if (ret < 0) {
		LOG_ERR("Failed to request connection to %s: %d", sta_ssid, ret);
		wifi_state = ap_addr_configured ? APP_WIFI_AP_ACTIVE : APP_WIFI_STA_RESTORE_PENDING;
	}

	return ret;
}

static int connect_to_stored_wifi(void)
{
	LOG_INF("Connecting with stored Wi-Fi credentials");
	wifi_state = APP_WIFI_STA_CONNECTING;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);

	if (ret < 0) {
		LOG_ERR("Failed to request stored Wi-Fi connection: %d", ret);
		wifi_state = APP_WIFI_IDLE;
	}

	return ret;
}

static int event_status(const struct net_mgmt_event_callback *cb)
{
	if (cb->info == NULL ||
	    cb->info_length < sizeof(((const struct wifi_status *)cb->info)->status)) {
		return -EIO;
	}

	return ((const struct wifi_status *)cb->info)->status;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		if (iface != sta_iface || wifi_state != APP_WIFI_STA_CONNECTING) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("Wi-Fi connection failed: %d", status);
			bool provisioning = sta_ssid_len > 0;
			bool ap_active = ap_addr_configured;

			clear_sta_credentials();
			if (provisioning && !ap_active) {
				wifi_state = APP_WIFI_STA_RESTORE_PENDING;
				k_work_reschedule(&wifi_work, K_MSEC(500));
			} else {
				wifi_state = provisioning ? APP_WIFI_AP_ACTIVE : APP_WIFI_IDLE;
			}
			if (!provisioning) {
				k_work_reschedule(&wifi_work, K_NO_WAIT);
			}
			break;
		}

		bool provisioning = sta_ssid_len > 0;

		if (provisioning) {
			LOG_INF("Connected to %s", sta_ssid);
			(void)save_sta_credentials();
		} else {
			LOG_INF("Connected with stored Wi-Fi credentials");
		}
		clear_sta_credentials();
		k_work_reschedule(&public_ip_check, K_SECONDS(30));
		if (provisioning && ap_addr_configured) {
			disable_ap_mode();
		} else {
			wifi_state = APP_WIFI_STA_ACTIVE;
			int ret = wifi_portal_start();

			if (ret < 0) {
				LOG_ERR("Failed to start Wi-Fi configuration portal: %d", ret);
			}
		}
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		if (iface != sta_iface) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_WRN("Wi-Fi disconnected with status %d", status);
		} else {
			LOG_INF("Wi-Fi disconnected");
		}
		k_work_cancel_delayable(&public_ip_check);

		if (wifi_state == APP_WIFI_STA_DISCONNECTING) {
			wifi_state = APP_WIFI_IDLE;
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		} else if (wifi_state == APP_WIFI_STA_CONNECTING) {
			bool provisioning = sta_ssid_len > 0;

			clear_sta_credentials();
			if (provisioning && !ap_addr_configured) {
				wifi_state = APP_WIFI_STA_RESTORE_PENDING;
				k_work_reschedule(&wifi_work, K_MSEC(500));
			} else {
				wifi_state = provisioning ? APP_WIFI_AP_ACTIVE : APP_WIFI_IDLE;
				if (!provisioning) {
					k_work_reschedule(&wifi_work, K_NO_WAIT);
				}
			}
		} else if (wifi_state == APP_WIFI_STA_ACTIVE) {
			clear_sta_credentials();
			wifi_state = APP_WIFI_IDLE;
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
		if (iface != ap_iface) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("AP enable failed: %d", status);
			wifi_state = APP_WIFI_IDLE;
			stop_ap_network();
			break;
		}

		wifi_state = APP_WIFI_AP_ACTIVE;
		int ret = wifi_portal_start();

		if (ret < 0) {
			LOG_ERR("Failed to start Wi-Fi configuration portal: %d", ret);
		} else {
			LOG_INF("AP %s ready at http://%s", CONFIG_OSKEY_WIFI_AP_SSID,
				CONFIG_OSKEY_WIFI_AP_IP_ADDRESS);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT: {
		if (iface != ap_iface) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("AP disable failed: %d", status);
			wifi_state = APP_WIFI_STA_ACTIVE;
			break;
		}

		stop_ap_network();
		wifi_state = APP_WIFI_STA_ACTIVE;
		break;
	}
	case NET_EVENT_WIFI_AP_STA_CONNECTED:
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		if (iface != ap_iface || cb->info == NULL ||
		    cb->info_length < sizeof(struct wifi_ap_sta_info)) {
			break;
		}

		const struct wifi_ap_sta_info *sta_info = cb->info;
		const char *action =
			mgmt_event == NET_EVENT_WIFI_AP_STA_CONNECTED ? "joined" : "left";

		LOG_INF("Station " MACSTR " %s", sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5],
			action);
		break;
	}
	default:
		break;
	}
}

static void wifi_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((wifi_state == APP_WIFI_AP_ACTIVE || wifi_state == APP_WIFI_IDLE) && sta_ssid_len > 0) {
		if (connect_to_wifi() < 0) {
			clear_sta_credentials();
			if (wifi_state == APP_WIFI_STA_RESTORE_PENDING) {
				k_work_reschedule(&wifi_work, K_MSEC(500));
			}
		}
		return;
	}

	if (wifi_state == APP_WIFI_STA_ACTIVE && sta_ssid_len > 0) {
		wifi_state = APP_WIFI_STA_DISCONNECTING;
		int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);

		if (ret < 0) {
			LOG_ERR("Failed to disconnect before changing Wi-Fi: %d", ret);
			wifi_state = APP_WIFI_STA_ACTIVE;
			clear_sta_credentials();
		}
		return;
	}

	if (wifi_state == APP_WIFI_STA_RESTORE_PENDING) {
		if (connect_to_stored_wifi() < 0) {
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		}
		return;
	}

	if (wifi_state != APP_WIFI_IDLE) {
		return;
	}

	(void)enable_ap_mode();
}

int wifi_start(void)
{
	net_mgmt_init_event_callback(&wifi_event_cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&wifi_event_cb);

	sta_iface = net_if_get_wifi_sta();
	if (sta_iface == NULL) {
		LOG_ERR("No Wi-Fi STA interface found");
		return -ENODEV;
	}

	ap_iface = net_if_get_wifi_sap();
	if (ap_iface == NULL) {
		LOG_ERR("No Wi-Fi AP interface found");
		return -ENODEV;
	}
	conn_mgr_ignore_iface(ap_iface);

	wifi_portal_init(provisioning_submitted);
	if (!wifi_credentials_is_empty() && connect_to_stored_wifi() == 0) {
		return 0;
	}

	return enable_ap_mode();
}

#else

int wifi_start(void)
{
#ifdef CONFIG_WIFI
	return -ENOTSUP;
#else
	return 0;
#endif
}

#endif /* CONFIG_WIFI && CONFIG_WIFI_USAGE_MODE_STA_AP */
