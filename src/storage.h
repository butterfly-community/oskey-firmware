#ifndef STORAGE_H
#define STORAGE_H

#include "wrapper.h"

int storage_init();
bool storage_general_check(uint16_t id);
bool storage_general_write(const uint8_t *data, int len, uint16_t id);
int storage_general_read(uint8_t *data, size_t len, uint16_t id);
int storage_erase();
#endif
