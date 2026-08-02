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
#include <zephyr/sys/atomic.h>

#include "http.h"
#include "wifi_portal.h"
#include "bus.h"

#include <zephyr/net/dhcpv4_server.h>

LOG_MODULE_REGISTER(wifi);

#define MACSTR               "%02X:%02X:%02X:%02X:%02X:%02X"
#define WIFI_CONNECT_TIMEOUT K_SECONDS(45)

#define NET_EVENT_WIFI_MASK                                                                        \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |                      \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

static struct net_if *sta_iface;
static struct net_mgmt_event_callback wifi_event_cb;
static atomic_t ap_state = ATOMIC_INIT(APP_WIFI_AP_OFF);
static atomic_t sta_state = ATOMIC_INIT(APP_WIFI_STA_DISCONNECTED);

struct pending_credentials {
	char ssid[WIFI_SSID_MAX_LEN + 1];
	char password[WIFI_PSK_MAX_LEN + 1];
	size_t ssid_len;
	size_t password_len;
};

static struct pending_credentials pending;
K_MUTEX_DEFINE(pending_lock);

static void credentials_clear(struct pending_credentials *credentials)
{
	volatile unsigned char *data = (volatile unsigned char *)credentials;

	for (size_t i = 0; i < sizeof(*credentials); ++i) {
		data[i] = 0;
	}
}

static void publish_state(void)
{
	struct app_wifi_state state = {
		.ap = atomic_get(&ap_state),
		.sta = atomic_get(&sta_state),
	};
	app_wifi_state_publish(&state);
}

static void set_ap_state(enum app_wifi_ap_state state)
{
	atomic_set(&ap_state, state);
	publish_state();
}

static void set_sta_state(enum app_wifi_sta_state state)
{
	atomic_set(&sta_state, state);
	publish_state();
}

static void wifi_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wifi_work, wifi_work_handler);
static void connection_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(connection_timeout, connection_timeout_handler);

static void pending_clear(void)
{
	k_mutex_lock(&pending_lock, K_FOREVER);
	credentials_clear(&pending);
	k_mutex_unlock(&pending_lock);
}

static bool pending_snapshot(struct pending_credentials *credentials)
{
	bool available;

	k_mutex_lock(&pending_lock, K_FOREVER);
	available = pending.ssid_len > 0;

	if (available && credentials != NULL) {
		*credentials = pending;
	}
	k_mutex_unlock(&pending_lock);

	return available;
}

static int store_credentials(const struct pending_credentials *credentials)
{
	enum wifi_security_type security =
		credentials->password_len == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;

	return wifi_credentials_set_personal(
		credentials->ssid, credentials->ssid_len, security, NULL, 0, credentials->password,
		credentials->password_len, WIFI_CREDENTIALS_FLAG_2_4GHz, WIFI_CHANNEL_ANY, 0);
}

static int save_pending_credentials(const struct pending_credentials *credentials)
{
	int ret = store_credentials(credentials);

	if (ret != -ENOBUFS) {
		if (ret < 0) {
			LOG_ERR("Failed to store Wi-Fi credentials: %d", ret);
		}
		return ret;
	}

	ret = wifi_credentials_delete_all();

	if (ret < 0) {
		LOG_ERR("Failed to replace stored Wi-Fi credentials: %d", ret);
		return ret;
	}

	ret = store_credentials(credentials);
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

	if (atomic_get(&ap_state) != APP_WIFI_AP_OFF) {
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

	set_ap_state(APP_WIFI_AP_STARTING);
	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &config, sizeof(config));
	if (ret < 0) {
		LOG_ERR("Failed to enable AP mode: %d", ret);
		set_ap_state(APP_WIFI_AP_OFF);
		stop_ap_network();
		k_work_reschedule(&wifi_work, K_SECONDS(1));
	}

	return ret;
}

static int provisioning_submitted(const char *ssid, size_t ssid_len, const char *password,
				  size_t password_len)
{
	k_mutex_lock(&pending_lock, K_FOREVER);
	if (pending.ssid_len > 0 || atomic_get(&sta_state) == APP_WIFI_STA_CONNECTING_NEW ||
	    atomic_get(&sta_state) == APP_WIFI_STA_DISCONNECTING) {
		k_mutex_unlock(&pending_lock);
		return -EBUSY;
	}

	memcpy(pending.ssid, ssid, ssid_len);
	pending.ssid[ssid_len] = '\0';
	pending.ssid_len = ssid_len;
	memcpy(pending.password, password, password_len);
	pending.password[password_len] = '\0';
	pending.password_len = password_len;
	k_mutex_unlock(&pending_lock);

	k_work_reschedule(&wifi_work, K_NO_WAIT);
	return 0;
}

static void disable_ap_mode(void)
{
	if (atomic_get(&ap_state) != APP_WIFI_AP_ACTIVE) {
		return;
	}

	LOG_INF("Stopping Wi-Fi AP");
	set_ap_state(APP_WIFI_AP_STOPPING);

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);

	if (ret < 0) {
		LOG_ERR("Failed to disable AP mode: %d", ret);
		set_ap_state(APP_WIFI_AP_ACTIVE);
		k_work_reschedule(&wifi_work, K_SECONDS(1));
	}
}

static int connect_to_new_wifi(void)
{
	struct wifi_connect_req_params config = {0};
	struct pending_credentials credentials;

	if (!pending_snapshot(&credentials)) {
		LOG_ERR("STA SSID is not configured");
		return -EINVAL;
	}

	config.ssid = (const uint8_t *)credentials.ssid;
	config.ssid_length = credentials.ssid_len;
	config.psk = (const uint8_t *)credentials.password;
	config.psk_length = credentials.password_len;
	config.security =
		credentials.password_len == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	config.channel = WIFI_CHANNEL_ANY;
	config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s", credentials.ssid);
	set_sta_state(APP_WIFI_STA_CONNECTING_NEW);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &config, sizeof(config));

	if (ret < 0) {
		LOG_ERR("Failed to request connection to %s: %d", credentials.ssid, ret);
		set_sta_state(APP_WIFI_STA_DISCONNECTED);
	} else {
		k_work_reschedule(&connection_timeout, WIFI_CONNECT_TIMEOUT);
	}
	credentials_clear(&credentials);

	return ret;
}

static int connect_to_stored_wifi(void)
{
	LOG_INF("Connecting with stored Wi-Fi credentials");
	set_sta_state(APP_WIFI_STA_CONNECTING_STORED);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);

	if (ret < 0) {
		LOG_ERR("Failed to request stored Wi-Fi connection: %d", ret);
		set_sta_state(APP_WIFI_STA_DISCONNECTED);
		k_work_reschedule(&wifi_work, K_SECONDS(10));
	} else {
		k_work_reschedule(&connection_timeout, WIFI_CONNECT_TIMEOUT);
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

static bool handle_sta_connection_failure(void)
{
	enum app_wifi_sta_state state = atomic_get(&sta_state);

	if ((state != APP_WIFI_STA_CONNECTING_NEW && state != APP_WIFI_STA_CONNECTING_STORED) ||
	    !atomic_cas(&sta_state, state, APP_WIFI_STA_DISCONNECTED)) {
		return false;
	}
	publish_state();
	if (state == APP_WIFI_STA_CONNECTING_NEW) {
		pending_clear();
	} else if (pending_snapshot(NULL)) {
		k_work_reschedule(&wifi_work, K_NO_WAIT);
	} else {
		k_work_reschedule(&wifi_work, K_SECONDS(10));
	}
	return true;
}

static bool sta_is_connecting(void)
{
	enum app_wifi_sta_state state = atomic_get(&sta_state);

	return state == APP_WIFI_STA_CONNECTING_STORED || state == APP_WIFI_STA_CONNECTING_NEW;
}

static void connection_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!handle_sta_connection_failure()) {
		return;
	}
	LOG_WRN("Wi-Fi connection timed out");
	int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);

	if (ret < 0 && ret != -ENOTCONN) {
		LOG_WRN("Failed to cancel timed-out Wi-Fi connection: %d", ret);
	}
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		if (iface != sta_iface) {
			break;
		}

		enum app_wifi_sta_state connecting = atomic_get(&sta_state);

		if (connecting != APP_WIFI_STA_CONNECTING_NEW &&
		    connecting != APP_WIFI_STA_CONNECTING_STORED) {
			break;
		}
		k_work_cancel_delayable(&connection_timeout);

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("Wi-Fi connection failed: %d", status);
			handle_sta_connection_failure();
			break;
		}

		bool new_credentials = connecting == APP_WIFI_STA_CONNECTING_NEW;

		if (!atomic_cas(&sta_state, connecting, APP_WIFI_STA_CONNECTED)) {
			break;
		}
		publish_state();

		if (new_credentials) {
			struct pending_credentials credentials;

			if (!pending_snapshot(&credentials)) {
				LOG_ERR("Connected without pending Wi-Fi credentials");
				set_sta_state(APP_WIFI_STA_DISCONNECTED);
				int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);

				if (ret < 0 && ret != -ENOTCONN) {
					LOG_WRN("Failed to disconnect unexpected Wi-Fi connection: "
						"%d",
						ret);
				}
				break;
			}
			LOG_INF("Connected to %s", credentials.ssid);
			if (save_pending_credentials(&credentials) < 0) {
				credentials_clear(&credentials);
				pending_clear();
				http_public_ip_check_schedule();
				LOG_WRN("Keeping the configuration AP active");
				break;
			}
			credentials_clear(&credentials);
		} else {
			LOG_INF("Connected with stored Wi-Fi credentials");
		}
		if (new_credentials) {
			pending_clear();
		}
		http_public_ip_check_schedule();
		k_work_reschedule(&wifi_work, K_NO_WAIT);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		if (iface != sta_iface) {
			break;
		}
		k_work_cancel_delayable(&connection_timeout);

		int status = event_status(cb);

		if (status != 0) {
			LOG_WRN("Wi-Fi disconnected with status %d", status);
		} else {
			LOG_INF("Wi-Fi disconnected");
		}
		http_public_ip_check_cancel();

		if (atomic_get(&sta_state) == APP_WIFI_STA_DISCONNECTING) {
			set_sta_state(APP_WIFI_STA_DISCONNECTED);
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		} else if (sta_is_connecting()) {
			handle_sta_connection_failure();
		} else if (atomic_get(&sta_state) == APP_WIFI_STA_CONNECTED) {
			set_sta_state(APP_WIFI_STA_DISCONNECTED);
			k_work_reschedule(&wifi_work, K_NO_WAIT);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
		if (iface != ap_iface || atomic_get(&ap_state) != APP_WIFI_AP_STARTING) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("AP enable failed: %d", status);
			set_ap_state(APP_WIFI_AP_OFF);
			stop_ap_network();
			k_work_reschedule(&wifi_work, K_SECONDS(1));
			break;
		}

		set_ap_state(APP_WIFI_AP_ACTIVE);
		LOG_INF("AP %s ready at http://%s", CONFIG_OSKEY_WIFI_AP_SSID,
			CONFIG_OSKEY_WIFI_AP_IP_ADDRESS);
		/* The result may arrive before the AP enable request returns. */
		k_work_reschedule(&wifi_work, K_TICKS(1));
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT: {
		if (iface != ap_iface || atomic_get(&ap_state) != APP_WIFI_AP_STOPPING) {
			break;
		}

		int status = event_status(cb);

		if (status != 0) {
			LOG_ERR("AP disable failed: %d", status);
			set_ap_state(APP_WIFI_AP_ACTIVE);
			k_work_reschedule(&wifi_work, K_SECONDS(1));
			break;
		}

		stop_ap_network();
		set_ap_state(APP_WIFI_AP_OFF);
		LOG_INF("Wi-Fi AP stopped");
		if (atomic_get(&sta_state) != APP_WIFI_STA_CONNECTED || pending_snapshot(NULL)) {
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

	if (atomic_get(&sta_state) == APP_WIFI_STA_CONNECTED) {
		if (!pending_snapshot(NULL)) {
			disable_ap_mode();
			return;
		}

		set_sta_state(APP_WIFI_STA_DISCONNECTING);
		int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);
		if (ret < 0) {
			LOG_ERR("Failed to disconnect before changing Wi-Fi: %d", ret);
			set_sta_state(APP_WIFI_STA_CONNECTED);
			pending_clear();
		}
		return;
	}

	if (atomic_get(&sta_state) != APP_WIFI_STA_DISCONNECTED) {
		return;
	}

	if (atomic_get(&ap_state) == APP_WIFI_AP_OFF) {
		(void)enable_ap_mode();
		return;
	}

	if (atomic_get(&ap_state) != APP_WIFI_AP_ACTIVE) {
		return;
	}

	if (pending_snapshot(NULL)) {
		if (connect_to_new_wifi() < 0) {
			pending_clear();
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
