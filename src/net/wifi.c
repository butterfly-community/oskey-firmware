// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2024 Muhammad Haziq
 */

#include "wifi.h"

#include <errno.h>
#include <string.h>

#ifdef CONFIG_OSKEY_WIFI

#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/posix/arpa/inet.h>

#include "http.h"
#include "wifi_portal.h"

#include <zephyr/net/dhcpv4_server.h>

LOG_MODULE_REGISTER(wifi);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define NET_EVENT_WIFI_MASK                                                                        \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |                      \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

enum app_wifi_ap_state {
	APP_WIFI_AP_OFF,
	APP_WIFI_AP_STARTING,
	APP_WIFI_AP_ACTIVE,
	APP_WIFI_AP_STOPPING,
};

enum app_wifi_sta_state {
	APP_WIFI_STA_DISCONNECTED,
	APP_WIFI_STA_CONNECTING_STORED,
	APP_WIFI_STA_CONNECTING_NEW,
	APP_WIFI_STA_CONNECTED,
	APP_WIFI_STA_DISCONNECTING,
};

static struct net_if *sta_iface;
static struct net_mgmt_event_callback wifi_event_cb;
static enum app_wifi_ap_state ap_state;
static enum app_wifi_sta_state sta_state;
static char pending_ssid[WIFI_SSID_MAX_LEN + 1];
static char pending_password[WIFI_PSK_MAX_LEN + 1];
static size_t pending_ssid_len;
static size_t pending_password_len;

static void wifi_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wifi_work, wifi_work_handler);

static void clear_pending_credentials(void)
{
	memset(pending_ssid, 0, sizeof(pending_ssid));
	memset(pending_password, 0, sizeof(pending_password));
	pending_ssid_len = 0;
	pending_password_len = 0;
}

static int save_pending_credentials(void)
{
	enum wifi_security_type security =
		pending_password_len == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	int ret = wifi_credentials_delete_all();

	if (ret < 0) {
		LOG_ERR("Failed to replace stored Wi-Fi credentials: %d", ret);
		return ret;
	}

	ret = wifi_credentials_set_personal(pending_ssid, pending_ssid_len, security, NULL, 0,
					    pending_password, pending_password_len,
					    WIFI_CREDENTIALS_FLAG_2_4GHz, WIFI_CHANNEL_ANY, 0);
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

	if (ap_state != APP_WIFI_AP_OFF) {
		return 0;
	}

	LOG_INF("Starting Wi-Fi AP");
	int ret = start_ap_network();

	if (ret < 0) {
		k_work_reschedule(&wifi_work, K_SECONDS(1));
		return ret;
	}

	config.ssid = (const uint8_t *)CONFIG_OSKEY_WIFI_AP_SSID;
	config.ssid_length = sizeof(CONFIG_OSKEY_WIFI_AP_SSID) - 1;
	config.psk = (const uint8_t *)CONFIG_OSKEY_WIFI_AP_PSK;
	config.psk_length = sizeof(CONFIG_OSKEY_WIFI_AP_PSK) - 1;
	config.security = config.psk_length == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	config.channel = WIFI_CHANNEL_ANY;
	config.band = WIFI_FREQ_BAND_2_4_GHZ;

	ap_state = APP_WIFI_AP_STARTING;
	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &config, sizeof(config));
	if (ret < 0) {
		LOG_ERR("Failed to enable AP mode: %d", ret);
		ap_state = APP_WIFI_AP_OFF;
		stop_ap_network();
		k_work_reschedule(&wifi_work, K_SECONDS(1));
	}

	return ret;
}

static void provisioning_submitted(const char *ssid, size_t ssid_len, const char *password,
				   size_t password_len)
{
	memcpy(pending_ssid, ssid, ssid_len);
	pending_ssid[ssid_len] = '\0';
	pending_ssid_len = ssid_len;

	memcpy(pending_password, password, password_len);
	pending_password[password_len] = '\0';
	pending_password_len = password_len;

	k_work_reschedule(&wifi_work, K_NO_WAIT);
}

static void disable_ap_mode(void)
{
	if (ap_state != APP_WIFI_AP_ACTIVE) {
		return;
	}

	LOG_INF("Stopping Wi-Fi AP");
	ap_state = APP_WIFI_AP_STOPPING;

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);

	if (ret < 0) {
		LOG_ERR("Failed to disable AP mode: %d", ret);
		ap_state = APP_WIFI_AP_ACTIVE;
		k_work_reschedule(&wifi_work, K_SECONDS(1));
	}
}

static int connect_to_new_wifi(void)
{
	struct wifi_connect_req_params config = {0};

	if (pending_ssid_len == 0) {
		LOG_ERR("STA SSID is not configured");
		return -EINVAL;
	}

	config.ssid = (const uint8_t *)pending_ssid;
	config.ssid_length = pending_ssid_len;
	config.psk = (const uint8_t *)pending_password;
	config.psk_length = pending_password_len;
	config.security =
		pending_password_len == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	config.channel = WIFI_CHANNEL_ANY;
	config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s", pending_ssid);
	sta_state = APP_WIFI_STA_CONNECTING_NEW;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &config, sizeof(config));

	if (ret < 0) {
		LOG_ERR("Failed to request connection to %s: %d", pending_ssid, ret);
		sta_state = APP_WIFI_STA_DISCONNECTED;
	}

	return ret;
}

static int connect_to_stored_wifi(void)
{
	LOG_INF("Connecting with stored Wi-Fi credentials");
	sta_state = APP_WIFI_STA_CONNECTING_STORED;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);

	if (ret < 0) {
		LOG_ERR("Failed to request stored Wi-Fi connection: %d", ret);
		sta_state = APP_WIFI_STA_DISCONNECTED;
		k_work_reschedule(&wifi_work, K_SECONDS(10));
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

static void handle_sta_connection_failure(void)
{
	bool new_credentials = sta_state == APP_WIFI_STA_CONNECTING_NEW;

	sta_state = APP_WIFI_STA_DISCONNECTED;
	if (new_credentials) {
		clear_pending_credentials();
	} else if (pending_ssid_len > 0) {
		k_work_reschedule(&wifi_work, K_NO_WAIT);
	} else {
		k_work_reschedule(&wifi_work, K_SECONDS(10));
	}
}

static bool sta_is_connecting(void)
{
	return sta_state == APP_WIFI_STA_CONNECTING_STORED ||
	       sta_state == APP_WIFI_STA_CONNECTING_NEW;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		if (iface != sta_iface || !sta_is_connecting()) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("Wi-Fi connection failed: %d", status);
			handle_sta_connection_failure();
			break;
		}

		bool new_credentials = sta_state == APP_WIFI_STA_CONNECTING_NEW;

		if (new_credentials) {
			LOG_INF("Connected to %s", pending_ssid);
			(void)save_pending_credentials();
		} else {
			LOG_INF("Connected with stored Wi-Fi credentials");
		}
		sta_state = APP_WIFI_STA_CONNECTED;
		if (new_credentials) {
			clear_pending_credentials();
		}
		http_public_ip_check_schedule();
		k_work_reschedule(&wifi_work, K_NO_WAIT);
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
		http_public_ip_check_cancel();

		if (sta_state == APP_WIFI_STA_DISCONNECTING) {
			sta_state = APP_WIFI_STA_DISCONNECTED;
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		} else if (sta_is_connecting()) {
			handle_sta_connection_failure();
		} else if (sta_state == APP_WIFI_STA_CONNECTED) {
			sta_state = APP_WIFI_STA_DISCONNECTED;
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
		if (iface != ap_iface || ap_state != APP_WIFI_AP_STARTING) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("AP enable failed: %d", status);
			ap_state = APP_WIFI_AP_OFF;
			stop_ap_network();
			k_work_reschedule(&wifi_work, K_SECONDS(1));
			break;
		}

		ap_state = APP_WIFI_AP_ACTIVE;
		LOG_INF("AP %s ready at http://%s", CONFIG_OSKEY_WIFI_AP_SSID,
			CONFIG_OSKEY_WIFI_AP_IP_ADDRESS);
		/* The result may arrive before the AP enable request returns. */
		k_work_reschedule(&wifi_work, K_TICKS(1));
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT: {
		if (iface != ap_iface || ap_state != APP_WIFI_AP_STOPPING) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("AP disable failed: %d", status);
			ap_state = APP_WIFI_AP_ACTIVE;
			k_work_reschedule(&wifi_work, K_SECONDS(1));
			break;
		}

		stop_ap_network();
		ap_state = APP_WIFI_AP_OFF;
		LOG_INF("Wi-Fi AP stopped");
		if (sta_state != APP_WIFI_STA_CONNECTED || pending_ssid_len > 0) {
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		}
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

	if (sta_state == APP_WIFI_STA_CONNECTED) {
		if (pending_ssid_len == 0) {
			disable_ap_mode();
			return;
		}

		sta_state = APP_WIFI_STA_DISCONNECTING;
		int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);
		if (ret < 0) {
			LOG_ERR("Failed to disconnect before changing Wi-Fi: %d", ret);
			sta_state = APP_WIFI_STA_CONNECTED;
			clear_pending_credentials();
		}
		return;
	}

	if (sta_state != APP_WIFI_STA_DISCONNECTED) {
		return;
	}

	if (ap_state == APP_WIFI_AP_OFF) {
		(void)enable_ap_mode();
		return;
	}

	if (ap_state != APP_WIFI_AP_ACTIVE) {
		return;
	}

	if (pending_ssid_len > 0) {
		if (connect_to_new_wifi() < 0) {
			clear_pending_credentials();
		}
		return;
	}

	if (!wifi_credentials_is_empty()) {
		(void)connect_to_stored_wifi();
	}
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

	int ret = wifi_portal_init(provisioning_submitted);

	if (ret < 0) {
		LOG_ERR("Failed to start Wi-Fi configuration portal: %d", ret);
		return ret;
	}

	return enable_ap_mode();
}

#else

int wifi_start(void)
{
	return 0;
}

#endif /* CONFIG_OSKEY_WIFI */
