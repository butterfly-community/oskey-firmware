#ifndef OSKEY_DISPLAY_H
#define OSKEY_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "bindings.h"

enum app_display_startup_state {
	APP_DISPLAY_SETUP,
	APP_DISPLAY_LOCKED,
	APP_DISPLAY_STORAGE_ERROR,
};

struct app_display_startup {
	enum app_display_startup_state state;
	uint8_t features[APP_FEATURE_COUNT];
};

enum app_display_wifi_state {
	APP_DISPLAY_WIFI_DISABLED,
	APP_DISPLAY_WIFI_DISCONNECTED,
	APP_DISPLAY_WIFI_AP,
	APP_DISPLAY_WIFI_CONNECTING,
	APP_DISPLAY_WIFI_CONNECTED,
};

enum app_display_bluetooth_state {
	APP_DISPLAY_BLUETOOTH_DISABLED,
	APP_DISPLAY_BLUETOOTH_IDLE,
	APP_DISPLAY_BLUETOOTH_ADVERTISING,
	APP_DISPLAY_BLUETOOTH_CONNECTED,
};

enum app_display_usb_state {
	APP_DISPLAY_USB_DISABLED,
	APP_DISPLAY_USB_DISCONNECTED,
	APP_DISPLAY_USB_ATTACHED,
	APP_DISPLAY_USB_CONFIGURED,
	APP_DISPLAY_USB_SUSPENDED,
};

struct app_display_status {
	enum app_display_wifi_state wifi;
	enum app_display_bluetooth_state bluetooth;
	enum app_display_usb_state usb;
};

int app_init_display(const struct app_display_startup *startup);
void app_display_message(DisplayAction action, AppError error, uint32_t value, const uint8_t *data,
			 size_t len);
void app_display_confirmation(const struct AppConfirmationView *confirmation);
void app_display_wifi_status(enum app_display_wifi_state state);
void app_display_bluetooth_status(enum app_display_bluetooth_state state);
void app_display_usb_status(enum app_display_usb_state state);

#endif
