#ifndef STORAGE_H
#define STORAGE_H

#include "wrapper.h"

int storage_init();
bool storage_seed_check();
bool storage_seed_write(const uint8_t *data, int len, int phrase_len);
int storage_seed_read(uint8_t *data, int len);
int storage_erase();
int storage_delete(uint16_t id);
#endif
