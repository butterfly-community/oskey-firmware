#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct storage_ids {
	uint16_t seed;
};

extern const struct storage_ids storage_ids;

int storage_init(void);
bool storage_ready(void);
bool storage_general_check(uint16_t id);
bool storage_general_write(const uint8_t *data, size_t len, uint16_t id);
int storage_general_read(uint8_t *data, size_t len, uint16_t id);
int storage_erase_flash(void);

#endif
