#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "wrapper.h"

int bt_init(void);
int bt_start(void);
int bt_nus_send_bytes(const uint8_t *data, size_t len);

#endif
