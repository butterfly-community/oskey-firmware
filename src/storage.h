#ifndef STORAGE_H
#define STORAGE_H

#include "wrapper.h"

#define STORAGE_ID_SEED 2
#define STORAGE_ID_PIN  10

int storage_init();
bool storage_general_check(uint16_t id);
bool storage_general_write(const uint8_t *data, int len, uint16_t id);
int storage_general_read(uint8_t *data, int len, uint16_t id);
bool storage_seed_write(const uint8_t *data, int len, int phrase_len);
int storage_seed_read(uint8_t *data, int len);
int storage_erase();
int storage_delete(uint16_t id);
#endif
