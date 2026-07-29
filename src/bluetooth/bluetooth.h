#ifndef OSKEY_BLUETOOTH_H
#define OSKEY_BLUETOOTH_H

#include "wrapper.h"

int oskey_bt_init(void);
int oskey_bt_start(void);
int oskey_bt_send(const uint8_t *data, size_t len);

#endif
