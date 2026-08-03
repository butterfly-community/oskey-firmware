#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"
#include "net/wifi.h"

static bool wifi_available(void)
{
	return ui.status.wifi.sta != APP_WIFI_STA_DISABLED;
}

static bool wifi_sta_running(void)
{
	return ui.status.wifi.sta != APP_WIFI_STA_DISABLED &&
	       ui.status.wifi.sta != APP_WIFI_STA_OFF;
}

static bool wifi_sta_busy(void)
{
	return ui.status.wifi.sta == APP_WIFI_STA_CONNECTING;
}

static int wifi_result(int ret, const char *error)
{
	if (ret < 0) {
		ui_error(error);
	}
	return ret;
}

static void wifi_restart(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_submit(LocalRequestKind_Restart, 0, NULL, 0, NULL, 0);
}

static void wifi_sta_toggled(lv_event_t *event)
{
	ARG_UNUSED(event);
	(void)wifi_result(app_wifi_radio_publish(true, !ui.wifi_config.sta_enabled),
			  "Could not change Wi-Fi station mode");
}

static void wifi_ap_toggled(lv_event_t *event)
{
	ARG_UNUSED(event);
	(void)wifi_result(app_wifi_radio_publish(false, !ui.wifi_config.ap_enabled),
			  "Could not change Wi-Fi access point");
}

static void wifi_scan_requested(lv_event_t *event)
{
	ARG_UNUSED(event);
	(void)wifi_result(app_wifi_scan_publish(), "Could not scan for Wi-Fi networks");
}

static void wifi_forget(void)
{
	(void)wifi_result(app_wifi_forget_network_publish(), "Could not forget Wi-Fi network");
}

static void wifi_forget_confirmed(lv_event_t *event)
{
	ARG_UNUSED(event);
	char message[96];
	snprintf(message, sizeof(message), "Remove the saved credentials for %s?",
		 ui.wifi_config.saved_ssid);
	ui_dialog_show(&oskey_trash, "Forget Wi-Fi network?", message, "Forget", UI_TONE_DANGER,
		       wifi_forget);
}

static const char *wifi_signal(int8_t rssi)
{
	if (rssi >= -55) {
		return "Excellent signal";
	}
	if (rssi >= -67) {
		return "Good signal";
	}
	if (rssi >= -75) {
		return "Fair signal";
	}
	return "Weak signal";
}

static const char *wifi_security(enum app_wifi_security security)
{
	switch (security) {
	case APP_WIFI_SECURITY_OPEN:
		return "Open";
	case APP_WIFI_SECURITY_SAE:
		return "WPA3";
	case APP_WIFI_SECURITY_PERSONAL:
		return "WPA/WPA2";
	case APP_WIFI_SECURITY_UNSUPPORTED:
	default:
		return "Unsupported security";
	}
}

static void wifi_network_selected(lv_event_t *event)
{
	size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
	if (index >= ui.wifi_scan.count) {
		return;
	}
	const struct app_wifi_network *network = &ui.wifi_scan.networks[index];

	if (strcmp(network->ssid, ui.wifi_config.saved_ssid) == 0) {
		if (!ui.wifi_config.sta_enabled) {
			(void)wifi_result(app_wifi_radio_publish(true, true),
					  "Could not enable the saved network");
		}
		return;
	}
	if (network->security == APP_WIFI_SECURITY_UNSUPPORTED) {
		ui_error("This Wi-Fi security mode is not supported");
		return;
	}

	memset(ui.wifi_ssid, 0, sizeof(ui.wifi_ssid));
	snprintf(ui.wifi_ssid, sizeof(ui.wifi_ssid), "%s", network->ssid);
	ui.wifi_security = network->security;
	if (network->security == APP_WIFI_SECURITY_OPEN) {
		(void)wifi_result(app_wifi_save_network_publish(ui.wifi_ssid, strlen(ui.wifi_ssid),
								NULL, 0, network->security),
				  "Could not save Wi-Fi network");
		return;
	}
	ui_push(UI_PAGE_WIFI_PASSWORD);
}

static const char *wifi_sta_detail(char *buffer, size_t size)
{
	switch (ui.status.wifi.sta) {
	case APP_WIFI_STA_CONNECTING:
		snprintf(buffer, size, "Connecting to %s", ui.status.wifi.connected_ssid);
		return buffer;
	case APP_WIFI_STA_CONNECTED:
		snprintf(buffer, size, "Connected to %s", ui.status.wifi.connected_ssid);
		return buffer;
	case APP_WIFI_STA_DISCONNECTED:
		return "Enabled but not connected";
	case APP_WIFI_STA_DISABLED:
		return "Wi-Fi support is not included in this firmware";
	case APP_WIFI_STA_OFF:
	default:
		return "Not running";
	}
}

static enum ui_tone wifi_sta_tone(void)
{
	switch (ui.status.wifi.sta) {
	case APP_WIFI_STA_CONNECTED:
		return UI_TONE_SUCCESS;
	case APP_WIFI_STA_CONNECTING:
		return UI_TONE_WARNING;
	case APP_WIFI_STA_DISCONNECTED:
		return UI_TONE_ACTIVE;
	case APP_WIFI_STA_DISABLED:
	case APP_WIFI_STA_OFF:
	default:
		return UI_TONE_MUTED;
	}
}

static const char *wifi_ap_detail(void)
{
	switch (ui.status.wifi.ap) {
	case APP_WIFI_AP_STARTING:
		return "Starting setup network";
	case APP_WIFI_AP_ACTIVE:
		return "Setup network is available";
	case APP_WIFI_AP_DISABLED:
		return "Wi-Fi support is not included in this firmware";
	case APP_WIFI_AP_OFF:
	default:
		return "Not running";
	}
}

static enum ui_tone wifi_ap_tone(void)
{
	switch (ui.status.wifi.ap) {
	case APP_WIFI_AP_ACTIVE:
		return UI_TONE_ACTIVE;
	case APP_WIFI_AP_STARTING:
		return UI_TONE_WARNING;
	case APP_WIFI_AP_DISABLED:
	case APP_WIFI_AP_OFF:
	default:
		return UI_TONE_MUTED;
	}
}

void ui_wifi_render(void)
{
	lv_obj_t *content = ui_page_begin("Wi-Fi", UI_NAVIGATION_BACK);
	char detail[96];

	ui_section(content, "STATUS");
	ui_list_row(content, &oskey_wifi, "Station", wifi_sta_detail(detail, sizeof(detail)), NULL,
		    wifi_sta_tone(), NULL, NULL);
	if (ui.status.wifi.sta == APP_WIFI_STA_CONNECTED &&
	    ui.status.public_ip.address[0] != '\0') {
		ui_list_row(content, NULL, "Public IP", ui.status.public_ip.address, NULL,
			    UI_TONE_SUCCESS, NULL, NULL);
	}
	ui_list_row(content, &oskey_wifi_ap, "Access point", wifi_ap_detail(), NULL, wifi_ap_tone(),
		    NULL, NULL);

	if (!wifi_available()) {
		return;
	}

	ui_section(content, "STARTUP CONFIGURATION");
	if (ui.wifi_config.restart_required) {
		ui_list_row(content, &oskey_refresh, "Restart required",
			    "Saved Wi-Fi changes are not active yet", "Restart", UI_TONE_WARNING,
			    wifi_restart, NULL);
	}
	ui_list_row(content, &oskey_wifi, "Station",
		    ui.wifi_config.sta_enabled ? "Starts automatically after restart"
					       : "Remains off after restart",
		    ui.wifi_config.sta_enabled ? "On" : "Off",
		    ui.wifi_config.sta_enabled ? UI_TONE_SUCCESS : UI_TONE_MUTED, wifi_sta_toggled,
		    NULL);
	ui_list_row(content, &oskey_wifi_ap, "Access point",
		    ui.wifi_config.ap_enabled ? "Starts automatically after restart"
					      : "Remains off after restart",
		    ui.wifi_config.ap_enabled ? "On" : "Off",
		    ui.wifi_config.ap_enabled ? UI_TONE_ACTIVE : UI_TONE_MUTED, wifi_ap_toggled,
		    NULL);

	if (ui.wifi_config.saved_ssid[0] != '\0') {
		ui_section(content, "SAVED NETWORK");
		bool connected =
			ui.status.wifi.sta == APP_WIFI_STA_CONNECTED &&
			strcmp(ui.wifi_config.saved_ssid, ui.status.wifi.connected_ssid) == 0;
		ui_list_row(content, &oskey_wifi, ui.wifi_config.saved_ssid,
			    connected ? "Connected" : "Credentials saved", NULL,
			    connected ? UI_TONE_SUCCESS : UI_TONE_DEFAULT, NULL, NULL);
		ui_list_row(content, &oskey_trash, "Forget network", "Remove saved credentials",
			    NULL, UI_TONE_DANGER, wifi_forget_confirmed, NULL);
	}

	ui_section(content, "AVAILABLE NETWORKS");
	if (!wifi_sta_running()) {
		ui_list_row(content, &oskey_wifi, "Station is not running",
			    ui.wifi_config.sta_enabled ? "Restart before scanning"
						       : "Enable it and restart before scanning",
			    NULL, UI_TONE_MUTED, NULL, NULL);
		return;
	}
	bool scanning = ui.wifi_scan.state == APP_WIFI_SCAN_SCANNING;
	ui_list_row(content, &oskey_refresh, scanning ? "Scanning…" : "Scan networks",
		    scanning ? "Looking for nearby Wi-Fi" : "Refresh the network list", NULL,
		    scanning ? UI_TONE_WARNING : UI_TONE_ACTIVE,
		    scanning || wifi_sta_busy() ? NULL : wifi_scan_requested, NULL);
	if (ui.wifi_scan.state == APP_WIFI_SCAN_ERROR) {
		ui_list_row(content, &oskey_warning, "Scan failed", "Try scanning again", NULL,
			    UI_TONE_WARNING, NULL, NULL);
		return;
	}
	if (ui.wifi_scan.state != APP_WIFI_SCAN_READY) {
		return;
	}
	if (ui.wifi_scan.count == 0) {
		ui_list_row(content, NULL, "No networks found", NULL, NULL, UI_TONE_MUTED, NULL,
			    NULL);
		return;
	}
	for (size_t i = 0; i < ui.wifi_scan.count; ++i) {
		const struct app_wifi_network *network = &ui.wifi_scan.networks[i];
		char network_detail[64];
		snprintf(network_detail, sizeof(network_detail), "%s · %s",
			 wifi_security(network->security), wifi_signal(network->rssi));
		bool saved = strcmp(network->ssid, ui.wifi_config.saved_ssid) == 0;
		ui_list_row(
			content, &oskey_wifi, network->ssid, network_detail, saved ? "Saved" : NULL,
			network->security == APP_WIFI_SECURITY_UNSUPPORTED ? UI_TONE_MUTED
									   : UI_TONE_DEFAULT,
			network->security == APP_WIFI_SECURITY_UNSUPPORTED ? NULL
									   : wifi_network_selected,
			(void *)(uintptr_t)i);
	}
}

void ui_wifi_password_render(void)
{
	char title[64];
	char hint[80];
	snprintf(title, sizeof(title), "Save %s", ui.wifi_ssid);
	snprintf(hint, sizeof(hint), "Enter the password for %s", ui.wifi_ssid);
	ui_input_page(title, hint, true);
}

void ui_wifi_password_submit(const char *password)
{
	size_t password_len = strlen(password);
	if (password_len < 8) {
		ui_input_error("Wi-Fi password must contain at least 8 characters");
		return;
	}
	if (app_wifi_save_network_publish(ui.wifi_ssid, strlen(ui.wifi_ssid), password,
					  password_len, ui.wifi_security) < 0) {
		ui_input_error("Could not save Wi-Fi network");
		return;
	}
	ui_back();
}

static const char *bluetooth_state_name(enum app_bluetooth_state state)
{
	switch (state) {
	case APP_BLUETOOTH_ADVERTISING:
		return "Advertising";
	case APP_BLUETOOTH_CONNECTED:
		return "Connected";
	case APP_BLUETOOTH_DISABLED:
		return "Unavailable";
	case APP_BLUETOOTH_IDLE:
	default:
		return "Idle";
	}
}

static const char *bluetooth_state_detail(enum app_bluetooth_state state)
{
	switch (state) {
	case APP_BLUETOOTH_ADVERTISING:
		return "Ready for a nearby device to connect";
	case APP_BLUETOOTH_CONNECTED:
		return "A Bluetooth device is connected";
	case APP_BLUETOOTH_DISABLED:
		return "Bluetooth support is not included in this firmware";
	case APP_BLUETOOTH_IDLE:
	default:
		return "No device is connected";
	}
}

static enum ui_tone bluetooth_tone(enum app_bluetooth_state state)
{
	switch (state) {
	case APP_BLUETOOTH_ADVERTISING:
		return UI_TONE_ACTIVE;
	case APP_BLUETOOTH_CONNECTED:
		return UI_TONE_SUCCESS;
	case APP_BLUETOOTH_IDLE:
	case APP_BLUETOOTH_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

void ui_bluetooth_render(void)
{
	lv_obj_t *content = ui_page_begin("Bluetooth", UI_NAVIGATION_BACK);
	enum app_bluetooth_state state = ui.status.bluetooth;

	ui_section(content, "STATUS");
	ui_list_row(content, &oskey_bluetooth, bluetooth_state_name(state),
		    bluetooth_state_detail(state), NULL, bluetooth_tone(state), NULL, NULL);

	ui_section(content, "SERVICE");
	ui_list_row(content, &oskey_bluetooth, "OSKey Bluetooth",
		    state == APP_BLUETOOTH_DISABLED
			    ? "Wireless transport is unavailable"
			    : "Encrypted wallet communication over Bluetooth",
		    NULL, state == APP_BLUETOOTH_DISABLED ? UI_TONE_MUTED : UI_TONE_DEFAULT, NULL,
		    NULL);
}

static const char *usb_state_name(enum app_usb_state state)
{
	switch (state) {
	case APP_USB_ATTACHED:
		return "Attached";
	case APP_USB_CONFIGURED:
		return "Connected";
	case APP_USB_SUSPENDED:
		return "Suspended";
	case APP_USB_DISABLED:
		return "Unavailable";
	case APP_USB_DISCONNECTED:
	default:
		return "Disconnected";
	}
}

static const char *usb_state_detail(enum app_usb_state state)
{
	switch (state) {
	case APP_USB_ATTACHED:
		return "USB detected; waiting for host configuration";
	case APP_USB_CONFIGURED:
		return "The USB host configured this device";
	case APP_USB_SUSPENDED:
		return "The USB host suspended this device";
	case APP_USB_DISABLED:
		return "USB support is not included in this firmware";
	case APP_USB_DISCONNECTED:
	default:
		return "No USB host is connected";
	}
}

static enum ui_tone usb_tone(enum app_usb_state state)
{
	switch (state) {
	case APP_USB_ATTACHED:
		return UI_TONE_ACTIVE;
	case APP_USB_CONFIGURED:
		return UI_TONE_SUCCESS;
	case APP_USB_SUSPENDED:
		return UI_TONE_WARNING;
	case APP_USB_DISCONNECTED:
	case APP_USB_DISABLED:
	default:
		return UI_TONE_MUTED;
	}
}

void ui_usb_render(void)
{
	lv_obj_t *content = ui_page_begin("USB", UI_NAVIGATION_BACK);
	enum app_usb_state state = ui.status.usb;

	ui_section(content, "STATUS");
	ui_list_row(content, &oskey_usb, usb_state_name(state), usb_state_detail(state), NULL,
		    usb_tone(state), NULL, NULL);

	ui_section(content, "INTERFACES");
	ui_list_row(content, &oskey_usb, "WebUSB", "Browser and application communication", NULL,
		    IS_ENABLED(CONFIG_OSKEY_USB) ? UI_TONE_DEFAULT : UI_TONE_MUTED, NULL, NULL);
	ui_list_row(content, &oskey_passkey, "FIDO2", "Passkeys and security-key operations", NULL,
		    IS_ENABLED(CONFIG_OSKEY_FIDO2) ? UI_TONE_DEFAULT : UI_TONE_MUTED, NULL, NULL);
}
