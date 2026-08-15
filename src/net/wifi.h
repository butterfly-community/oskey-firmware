/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stddef.h>

#include "bus.h"

int wifi_start(void);

/* Return values cover command validation and bus publication, not command execution. */
int app_wifi_radio_publish(bool station, bool enabled);
int app_wifi_scan_publish(void);
int app_wifi_forget_network_publish(void);
int app_wifi_save_network_publish(const char *ssid, size_t ssid_len, const char *password,
				  size_t password_len, enum app_wifi_security security);

#endif
