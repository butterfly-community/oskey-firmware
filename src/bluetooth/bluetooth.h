/* SPDX-License-Identifier: MPL-2.0 */

#ifndef OSKEY_BLUETOOTH_H
#define OSKEY_BLUETOOTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int oskey_bt_init(void);
int oskey_bt_start(void);
int oskey_bt_send(uint32_t session_id, const uint8_t *data, size_t len);
#ifdef CONFIG_OSKEY_BLUETOOTH
bool oskey_bt_address_privacy_enabled(void);
int oskey_bt_address_privacy_set(bool enabled);
#endif

#endif
