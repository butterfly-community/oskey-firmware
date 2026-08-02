#ifndef OSKEY_DISPLAY_H
#define OSKEY_DISPLAY_H

#include <stdbool.h>

#include "bus.h"

struct app_display_status {
	struct app_wifi_state wifi;
	enum app_bluetooth_state bluetooth;
	enum app_usb_state usb;
	enum app_storage_state storage;
	enum WalletState wallet;
};

int app_init_display(void);
bool app_display_ready(void);

#endif
