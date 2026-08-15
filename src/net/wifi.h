/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIFI_H
#define WIFI_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include "bus.h"

#ifdef CONFIG_OSKEY_WIFI
/* Return values cover command validation and bus publication, not command execution. */
int wifi_start(void);
int app_wifi_radio_publish(bool station, bool enabled);
int app_wifi_scan_publish(void);
int app_wifi_forget_network_publish(void);
int app_wifi_save_network_publish(const char *ssid, size_t ssid_len, const char *password,
				  size_t password_len, enum app_wifi_security security);
#else
static inline int wifi_start(void)
{
	return 0;
}

static inline int app_wifi_radio_publish(bool station, bool enabled)
{
	(void)station;
	(void)enabled;
	return -ENOTSUP;
}

static inline int app_wifi_scan_publish(void)
{
	return -ENOTSUP;
}

static inline int app_wifi_forget_network_publish(void)
{
	return -ENOTSUP;
}

static inline int app_wifi_save_network_publish(const char *ssid, size_t ssid_len,
						const char *password, size_t password_len,
						enum app_wifi_security security)
{
	(void)ssid;
	(void)ssid_len;
	(void)password;
	(void)password_len;
	(void)security;
	return -ENOTSUP;
}
#endif

#endif
