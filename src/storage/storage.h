#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

int storage_init();
bool storage_seed_check();
int storage_seed_write_buffer(const uint8_t *data, int len);
int storage_seed_read_buffer(uint8_t *data, int len);

#endif
