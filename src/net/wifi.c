/*
 * Copyright (c) 2024 Muhammad Haziq
 * Copyright (c) 2026 OSKey contributors
 *
 * Modified by OSKey contributors in 2026.
 * SPDX-License-Identifier: Apache-2.0
 */

#define _DEFAULT_SOURCE

#include "wifi.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(wifi);

static int wifi_command_publish(struct app_wifi_command *command)
{
	if (!IS_ENABLED(CONFIG_OSKEY_WIFI)) {
		return -ENOTSUP;
	}
	if (command == NULL || command->kind <= APP_WIFI_COMMAND_NONE ||
	    command->kind > APP_WIFI_COMMAND_FORGET_NETWORK) {
		return -EINVAL;
	}

	int ret = zbus_chan_pub(&app_wifi_command_chan, command, K_FOREVER);
	explicit_bzero(command->password, sizeof(command->password));

	/* The synchronous listener consumes credentials before this scrub. */
	int claim_ret = zbus_chan_claim(&app_wifi_command_chan, K_FOREVER);
	if (claim_ret == 0) {
		struct app_wifi_command *stored = zbus_chan_msg(&app_wifi_command_chan);

		explicit_bzero(stored->password, sizeof(stored->password));
		stored->password_len = 0;
		(void)zbus_chan_finish(&app_wifi_command_chan);
	} else {
		LOG_ERR("Failed to clear Wi-Fi command credentials: %d", claim_ret);
	}
	return ret;
}

int app_wifi_radio_publish(bool station, bool enabled)
{
	struct app_wifi_command command = {
		.kind = station ? APP_WIFI_COMMAND_SET_STA : APP_WIFI_COMMAND_SET_AP,
		.enabled = enabled,
	};
	return wifi_command_publish(&command);
}

int app_wifi_scan_publish(void)
{
	struct app_wifi_command command = {.kind = APP_WIFI_COMMAND_SCAN};
	return wifi_command_publish(&command);
}

int app_wifi_forget_network_publish(void)
{
	struct app_wifi_command command = {.kind = APP_WIFI_COMMAND_FORGET_NETWORK};
	return wifi_command_publish(&command);
}

int app_wifi_save_network_publish(const char *ssid, size_t ssid_len, const char *password,
				  size_t password_len, enum app_wifi_security security)
{
	if (!IS_ENABLED(CONFIG_OSKEY_WIFI)) {
		return -ENOTSUP;
	}
	if (ssid == NULL || ssid_len == 0 || ssid_len > APP_WIFI_SSID_MAX_LEN ||
	    password_len > APP_WIFI_PASSWORD_MAX_LEN || (password_len > 0 && password == NULL) ||
	    security < APP_WIFI_SECURITY_OPEN || security > APP_WIFI_SECURITY_SAE) {
		return -EINVAL;
	}
	if (security != APP_WIFI_SECURITY_OPEN && password_len < 8) {
		return -EINVAL;
	}

	struct app_wifi_command command = {
		.kind = APP_WIFI_COMMAND_SAVE_NETWORK,
		.security = security,
		.ssid_len = ssid_len,
		.password_len = password_len,
	};
	memcpy(command.ssid, ssid, ssid_len);
	if (password_len > 0) {
		memcpy(command.password, password, password_len);
	}
	return wifi_command_publish(&command);
}

#ifdef CONFIG_OSKEY_WIFI

#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

#include "wifi_portal.h"

#define MACSTR               "%02X:%02X:%02X:%02X:%02X:%02X"
#define WIFI_CONNECT_TIMEOUT K_SECONDS(45)
#define WIFI_SETTINGS_STA    "wifi/sta_enabled"
#define WIFI_SETTINGS_AP     "wifi/ap_enabled"

#define NET_EVENT_WIFI_MASK                                                                        \
	(NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE | NET_EVENT_WIFI_CONNECT_RESULT |   \
	 NET_EVENT_WIFI_DISCONNECT_RESULT | NET_EVENT_WIFI_AP_ENABLE_RESULT |                      \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

static struct net_if *sta_iface;
static struct net_if *ap_iface;
static struct net_mgmt_event_callback wifi_event_cb;
static struct net_mgmt_event_callback ipv4_event_cb;
static atomic_t ap_state = ATOMIC_INIT(APP_WIFI_AP_OFF);
static atomic_t sta_state = ATOMIC_INIT(APP_WIFI_STA_OFF);
static atomic_t ap_client_count;
static atomic_t scan_active;
static struct app_wifi_config wifi_config;
static struct app_dhcp_info dhcp_info;
static char connected_ssid[APP_WIFI_SSID_MAX_LEN + 1];
static struct app_wifi_scan scan = {
	.state = APP_WIFI_SCAN_IDLE,
};

static struct net_in_addr ap_addr;
static bool ap_addr_configured;
static bool dhcp_server_started;

static int enable_ap_mode(void);
static int start_scan(void);
static void connection_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(connection_timeout, connection_timeout_handler);

BUILD_ASSERT(sizeof(CONFIG_OSKEY_WIFI_AP_SSID) > 1, "OSKey Wi-Fi AP SSID is empty");
BUILD_ASSERT(sizeof(CONFIG_OSKEY_WIFI_AP_SSID) - 1 <= WIFI_SSID_MAX_LEN,
	     "OSKey Wi-Fi AP SSID is too long");
BUILD_ASSERT(sizeof(CONFIG_OSKEY_WIFI_AP_PSK) == 1 ||
		     (sizeof(CONFIG_OSKEY_WIFI_AP_PSK) >= 9 &&
		      sizeof(CONFIG_OSKEY_WIFI_AP_PSK) <= WIFI_PSK_MAX_LEN),
	     "OSKey Wi-Fi AP password must be empty or 8-63 bytes");

static int wifi_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	bool *preference;

	if (strcmp(name, "sta_enabled") == 0) {
		preference = &wifi_config.sta_enabled;
	} else if (strcmp(name, "ap_enabled") == 0) {
		preference = &wifi_config.ap_enabled;
	} else {
		return -ENOENT;
	}
	if (len != sizeof(uint8_t)) {
		return -EINVAL;
	}

	uint8_t enabled;
	ssize_t ret = read_cb(cb_arg, &enabled, sizeof(enabled));

	if (ret < 0) {
		return (int)ret;
	}
	if (ret != sizeof(enabled) || enabled > 1) {
		return -EINVAL;
	}
	*preference = enabled != 0;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(wifi_preferences, "wifi", NULL, wifi_settings_set, NULL, NULL);

static void publish_config(void)
{
	struct app_network_event event = {
		.kind = APP_NETWORK_EVENT_WIFI_CONFIG,
		.data.config = wifi_config,
	};
	int ret = zbus_chan_pub(&app_network_event_chan, &event, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish Wi-Fi configuration: %d", ret);
	}
}

static void publish_state(void)
{
	struct app_wifi_state state = {
		.ap = atomic_get(&ap_state),
		.sta = atomic_get(&sta_state),
		.ap_client_connected = atomic_get(&ap_client_count) > 0,
		.dhcp = dhcp_info,
	};
	memcpy(state.connected_ssid, connected_ssid, sizeof(state.connected_ssid));

	struct app_network_event event = {
		.kind = APP_NETWORK_EVENT_WIFI_STATE,
		.data.wifi = state,
	};
	int ret = zbus_chan_pub(&app_network_event_chan, &event, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish Wi-Fi state: %d", ret);
	}
}

static void set_ap_state(enum app_wifi_ap_state state)
{
	if (state != APP_WIFI_AP_ACTIVE) {
		atomic_clear(&ap_client_count);
	}
	atomic_set(&ap_state, state);
	publish_state();
}

static void set_sta_state(enum app_wifi_sta_state state)
{
	if (state != APP_WIFI_STA_CONNECTED) {
		memset(&dhcp_info, 0, sizeof(dhcp_info));
	}
	atomic_set(&sta_state, state);
	publish_state();
}

static void dhcp_address_read(struct net_if *iface, struct net_if_addr *if_addr, void *user_data)
{
	ARG_UNUSED(user_data);
	if (if_addr->addr_type != NET_ADDR_DHCP || dhcp_info.address[0] != '\0') {
		return;
	}

	struct net_in_addr netmask =
		net_if_ipv4_get_netmask_by_addr(iface, &if_addr->address.in_addr);
	struct net_in_addr gateway = net_if_ipv4_get_gw(iface);

	(void)net_addr_ntop(AF_INET, &if_addr->address.in_addr, dhcp_info.address,
			    sizeof(dhcp_info.address));
	(void)net_addr_ntop(AF_INET, &netmask, dhcp_info.netmask, sizeof(dhcp_info.netmask));
	(void)net_addr_ntop(AF_INET, &gateway, dhcp_info.gateway, sizeof(dhcp_info.gateway));
	dhcp_info.lease_seconds = iface->config.dhcpv4.lease_time;
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	ARG_UNUSED(cb);
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD || iface != sta_iface) {
		return;
	}

	memset(&dhcp_info, 0, sizeof(dhcp_info));
	net_if_ipv4_addr_foreach(iface, dhcp_address_read, NULL);
	if (dhcp_info.address[0] != '\0') {
		LOG_INF("DHCP address %s, subnet %s, gateway %s, lease %u seconds",
			dhcp_info.address, dhcp_info.netmask, dhcp_info.gateway,
			dhcp_info.lease_seconds);
		publish_state();
	}
}

static void scan_publish(void)
{
	struct app_network_event event = {
		.kind = APP_NETWORK_EVENT_WIFI_SCAN,
		.data.scan = scan,
	};
	int ret = zbus_chan_pub(&app_network_event_chan, &event, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish Wi-Fi scan: %d", ret);
	}
}

static void saved_ssid_set(const char *ssid, size_t ssid_len)
{
	memset(wifi_config.saved_ssid, 0, sizeof(wifi_config.saved_ssid));
	if (ssid != NULL && ssid_len > 0) {
		memcpy(wifi_config.saved_ssid, ssid,
		       MIN(ssid_len, sizeof(wifi_config.saved_ssid) - 1));
	}
	publish_config();
}

static void load_saved_ssid(void *unused, const char *ssid, size_t ssid_len)
{
	ARG_UNUSED(unused);
	if (wifi_config.saved_ssid[0] == '\0') {
		memcpy(wifi_config.saved_ssid, ssid,
		       MIN(ssid_len, sizeof(wifi_config.saved_ssid) - 1));
	}
}

static void saved_ssid_load(void)
{
	memset(wifi_config.saved_ssid, 0, sizeof(wifi_config.saved_ssid));
	wifi_credentials_for_each_ssid(load_saved_ssid, NULL);
}

static enum wifi_security_type wifi_security(enum app_wifi_security security)
{
	switch (security) {
	case APP_WIFI_SECURITY_OPEN:
		return WIFI_SECURITY_TYPE_NONE;
	case APP_WIFI_SECURITY_SAE:
		return WIFI_SECURITY_TYPE_SAE;
	case APP_WIFI_SECURITY_PERSONAL:
	case APP_WIFI_SECURITY_UNSUPPORTED:
	default:
		return WIFI_SECURITY_TYPE_PSK;
	}
}

static enum app_wifi_security app_security(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return APP_WIFI_SECURITY_OPEN;
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_SAE_H2E:
	case WIFI_SECURITY_TYPE_SAE_AUTO:
		return APP_WIFI_SECURITY_SAE;
	case WIFI_SECURITY_TYPE_PSK:
	case WIFI_SECURITY_TYPE_PSK_SHA256:
	case WIFI_SECURITY_TYPE_WPA_PSK:
	case WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL:
		return APP_WIFI_SECURITY_PERSONAL;
	default:
		return APP_WIFI_SECURITY_UNSUPPORTED;
	}
}

static int credentials_store(const char *ssid, size_t ssid_len, const char *password,
			     size_t password_len, enum app_wifi_security security)
{
	int ret = wifi_credentials_set_personal(ssid, ssid_len, wifi_security(security), NULL, 0,
						password, password_len,
						WIFI_CREDENTIALS_FLAG_2_4GHz, WIFI_CHANNEL_ANY, 0);

	if (ret == -ENOBUFS) {
		ret = wifi_credentials_delete_all();
		if (ret == 0) {
			ret = wifi_credentials_set_personal(
				ssid, ssid_len, wifi_security(security), NULL, 0, password,
				password_len, WIFI_CREDENTIALS_FLAG_2_4GHz, WIFI_CHANNEL_ANY, 0);
		}
	}
	if (ret < 0) {
		LOG_ERR("Failed to store Wi-Fi credentials: %d", ret);
	}
	return ret;
}

static int preference_store(bool *preference, const char *name, bool enabled)
{
	uint8_t value = enabled;
	int ret = settings_save_one(name, &value, sizeof(value));

	if (ret < 0) {
		LOG_ERR("Failed to save Wi-Fi preference %s: %d", name, ret);
		return ret;
	}
	*preference = enabled;
	wifi_config.restart_required = true;
	publish_config();
	return 0;
}

static int network_configure(const char *ssid, size_t ssid_len, const char *password,
			     size_t password_len, enum app_wifi_security security)
{
	int ret = credentials_store(ssid, ssid_len, password, password_len, security);

	if (ret < 0) {
		return ret;
	}
	uint8_t enabled = 1;
	ret = settings_save_one(WIFI_SETTINGS_STA, &enabled, sizeof(enabled));
	if (ret < 0) {
		LOG_ERR("Failed to enable Wi-Fi station preference: %d", ret);
		return ret;
	}
	wifi_config.sta_enabled = true;
	wifi_config.restart_required = true;
	saved_ssid_set(ssid, ssid_len);
	return 0;
}

static void wifi_command_received(const struct zbus_channel *channel)
{
	const struct app_wifi_command *command = zbus_chan_const_msg(channel);

	switch (command->kind) {
	case APP_WIFI_COMMAND_SET_STA:
		(void)preference_store(&wifi_config.sta_enabled, WIFI_SETTINGS_STA,
				       command->enabled);
		break;
	case APP_WIFI_COMMAND_SET_AP:
		(void)preference_store(&wifi_config.ap_enabled, WIFI_SETTINGS_AP, command->enabled);
		break;
	case APP_WIFI_COMMAND_SCAN:
		if (atomic_get(&sta_state) != APP_WIFI_STA_OFF &&
		    atomic_get(&sta_state) != APP_WIFI_STA_DISABLED &&
		    atomic_get(&sta_state) != APP_WIFI_STA_CONNECTING) {
			(void)start_scan();
		}
		break;
	case APP_WIFI_COMMAND_SAVE_NETWORK:
		(void)network_configure(command->ssid, command->ssid_len, command->password,
					command->password_len, command->security);
		break;
	case APP_WIFI_COMMAND_FORGET_NETWORK: {
		int ret = wifi_credentials_delete_all();
		if (ret < 0) {
			LOG_ERR("Failed to forget Wi-Fi credentials: %d", ret);
			break;
		}
		wifi_config.restart_required = true;
		saved_ssid_set(NULL, 0);
		break;
	}
	case APP_WIFI_COMMAND_NONE:
	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(wifi_command_listener, wifi_command_received);
ZBUS_CHAN_ADD_OBS(app_wifi_command_chan, wifi_command_listener, 0);

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

	if (inet_pton(AF_INET, CONFIG_OSKEY_WIFI_AP_IP_ADDRESS, &ap_addr) != 1 ||
	    inet_pton(AF_INET, CONFIG_OSKEY_WIFI_AP_NETMASK, &netmask) != 1) {
		LOG_ERR("Invalid AP network configuration");
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
	if (atomic_get(&ap_state) != APP_WIFI_AP_OFF) {
		return 0;
	}
	LOG_INF("Starting Wi-Fi AP");
	int ret = start_ap_network();
	if (ret < 0) {
		return ret;
	}

	struct wifi_connect_req_params config = {
		.ssid = (const uint8_t *)CONFIG_OSKEY_WIFI_AP_SSID,
		.ssid_length = sizeof(CONFIG_OSKEY_WIFI_AP_SSID) - 1,
		.psk = (const uint8_t *)CONFIG_OSKEY_WIFI_AP_PSK,
		.psk_length = sizeof(CONFIG_OSKEY_WIFI_AP_PSK) - 1,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};
	config.security = config.psk_length == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;

	set_ap_state(APP_WIFI_AP_STARTING);
	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &config, sizeof(config));
	if (ret < 0) {
		LOG_ERR("Failed to enable AP mode: %d", ret);
		set_ap_state(APP_WIFI_AP_OFF);
		stop_ap_network();
	}
	return ret;
}

static void headless_ap_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!IS_ENABLED(CONFIG_OSKEY_DISPLAY)) {
		(void)enable_ap_mode();
	}
}

static K_WORK_DEFINE(headless_ap_work, headless_ap_handler);

static void headless_fallback(void)
{
	if (!IS_ENABLED(CONFIG_OSKEY_DISPLAY)) {
		k_work_submit(&headless_ap_work);
	}
}

static int provisioning_submitted(const char *ssid, size_t ssid_len, const char *password,
				  size_t password_len)
{
	return app_wifi_save_network_publish(ssid, ssid_len, password, password_len,
					     password_len == 0 ? APP_WIFI_SECURITY_OPEN
							       : APP_WIFI_SECURITY_PERSONAL);
}

static int connect_to_stored_wifi(void)
{
	if (wifi_config.saved_ssid[0] == '\0') {
		return -ENOENT;
	}
	LOG_INF("Connecting with stored Wi-Fi credentials");
	snprintk(connected_ssid, sizeof(connected_ssid), "%s", wifi_config.saved_ssid);
	set_sta_state(APP_WIFI_STA_CONNECTING);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Failed to request stored Wi-Fi connection: %d", ret);
		set_sta_state(APP_WIFI_STA_DISCONNECTED);
		headless_fallback();
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

static void connection_failed(void)
{
	if (!atomic_cas(&sta_state, APP_WIFI_STA_CONNECTING, APP_WIFI_STA_DISCONNECTED)) {
		return;
	}
	publish_state();
	headless_fallback();
}

static void connection_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (atomic_get(&sta_state) != APP_WIFI_STA_CONNECTING) {
		return;
	}
	LOG_WRN("Wi-Fi connection timed out");
	connection_failed();
	int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);
	if (ret < 0 && ret != -ENOTCONN) {
		LOG_WRN("Failed to cancel timed-out Wi-Fi connection: %d", ret);
	}
}

static int start_scan(void)
{
	struct wifi_scan_params params = {0};

	if (!atomic_cas(&scan_active, 0, 1)) {
		return -EBUSY;
	}
	memset(scan.networks, 0, sizeof(scan.networks));
	scan.count = 0;
	scan.state = APP_WIFI_SCAN_SCANNING;
	scan.generation++;
	scan_publish();

	int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, sta_iface, &params, sizeof(params));
	if (ret < 0) {
		LOG_ERR("Failed to request Wi-Fi scan: %d", ret);
		scan.state = APP_WIFI_SCAN_ERROR;
		scan.generation++;
		atomic_set(&scan_active, 0);
		scan_publish();
	}
	return ret;
}

static void scan_result_add(const struct wifi_scan_result *result)
{
	if (result->ssid_length == 0 || result->ssid_length > APP_WIFI_SSID_MAX_LEN) {
		return;
	}
	size_t index = scan.count;
	for (size_t i = 0; i < scan.count; ++i) {
		if (strlen(scan.networks[i].ssid) == result->ssid_length &&
		    memcmp(scan.networks[i].ssid, result->ssid, result->ssid_length) == 0) {
			if (result->rssi <= scan.networks[i].rssi) {
				return;
			}
			index = i;
			break;
		}
	}
	if (index == scan.count) {
		if (scan.count >= ARRAY_SIZE(scan.networks)) {
			index = 0;
			for (size_t i = 1; i < scan.count; ++i) {
				if (scan.networks[i].rssi < scan.networks[index].rssi) {
					index = i;
				}
			}
			if (result->rssi <= scan.networks[index].rssi) {
				return;
			}
		} else {
			scan.count++;
		}
	}

	struct app_wifi_network *network = &scan.networks[index];
	memset(network, 0, sizeof(*network));
	memcpy(network->ssid, result->ssid, result->ssid_length);
	network->rssi = result->rssi;
	network->security = app_security(result->security);
}

static void scan_sort(void)
{
	for (size_t i = 1; i < scan.count; ++i) {
		struct app_wifi_network network = scan.networks[i];
		size_t position = i;

		while (position > 0 && scan.networks[position - 1].rssi < network.rssi) {
			scan.networks[position] = scan.networks[position - 1];
			position--;
		}
		scan.networks[position] = network;
	}
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		if (iface == sta_iface && atomic_get(&scan_active) != 0 && cb->info != NULL &&
		    cb->info_length >= sizeof(struct wifi_scan_result)) {
			scan_result_add(cb->info);
		}
		break;
	case NET_EVENT_WIFI_SCAN_DONE: {
		if (iface != sta_iface || !atomic_cas(&scan_active, 1, 0)) {
			break;
		}
		int status = event_status(cb);
		scan_sort();
		scan.state = status == 0 ? APP_WIFI_SCAN_READY : APP_WIFI_SCAN_ERROR;
		scan.generation++;
		scan_publish();
		break;
	}
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		if (iface != sta_iface || atomic_get(&sta_state) != APP_WIFI_STA_CONNECTING) {
			break;
		}
		k_work_cancel_delayable(&connection_timeout);
		int status = event_status(cb);
		if (status != 0) {
			LOG_ERR("Wi-Fi connection failed: %d", status);
			connection_failed();
			break;
		}
		set_sta_state(APP_WIFI_STA_CONNECTED);
		LOG_INF("Connected with stored Wi-Fi credentials");
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		enum app_wifi_sta_state state = atomic_get(&sta_state);
		if (iface == sta_iface &&
		    (state == APP_WIFI_STA_CONNECTING || state == APP_WIFI_STA_CONNECTED)) {
			k_work_cancel_delayable(&connection_timeout);
			set_sta_state(APP_WIFI_STA_DISCONNECTED);
			headless_fallback();
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
			stop_ap_network();
			set_ap_state(APP_WIFI_AP_OFF);
			break;
		}
		set_ap_state(APP_WIFI_AP_ACTIVE);
		LOG_INF("AP %s ready at http://%s", CONFIG_OSKEY_WIFI_AP_SSID,
			CONFIG_OSKEY_WIFI_AP_IP_ADDRESS);
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
		if (mgmt_event == NET_EVENT_WIFI_AP_STA_CONNECTED) {
			atomic_inc(&ap_client_count);
		} else if (atomic_get(&ap_client_count) > 0) {
			atomic_dec(&ap_client_count);
		}
		publish_state();
		break;
	}
	default:
		break;
	}
}

int wifi_start(void)
{
	net_mgmt_init_event_callback(&wifi_event_cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&wifi_event_cb);
	net_mgmt_init_event_callback(&ipv4_event_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_event_cb);

	sta_iface = net_if_get_wifi_sta();
	ap_iface = net_if_get_wifi_sap();
	if (sta_iface == NULL || ap_iface == NULL) {
		LOG_ERR("Wi-Fi interfaces are unavailable");
		return -ENODEV;
	}
	conn_mgr_ignore_iface(ap_iface);

	int ret = wifi_portal_init(provisioning_submitted);
	if (ret < 0) {
		LOG_ERR("Failed to start Wi-Fi configuration portal: %d", ret);
		return ret;
	}

	saved_ssid_load();
	bool has_credentials = wifi_config.saved_ssid[0] != '\0';
	publish_config();
	publish_state();
	scan_publish();

	bool start_ap =
		IS_ENABLED(CONFIG_OSKEY_DISPLAY) ? wifi_config.ap_enabled : !has_credentials;
	bool start_sta =
		IS_ENABLED(CONFIG_OSKEY_DISPLAY) ? wifi_config.sta_enabled : has_credentials;
	if (start_ap) {
		(void)enable_ap_mode();
	}
	if (start_sta) {
		set_sta_state(APP_WIFI_STA_DISCONNECTED);
		if (has_credentials) {
			(void)connect_to_stored_wifi();
		}
	}
	return 0;
}

#else

int wifi_start(void)
{
	return 0;
}

#endif /* CONFIG_OSKEY_WIFI */
