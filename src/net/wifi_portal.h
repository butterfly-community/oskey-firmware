/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <stddef.h>

typedef void (*wifi_portal_submit_cb_t)(const char *ssid, size_t ssid_len, const char *password,
					size_t password_len);

void wifi_portal_init(wifi_portal_submit_cb_t submit_cb);
int wifi_portal_start(void);

#endif
