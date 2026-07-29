#ifndef STORAGE_H
#define STORAGE_H

#include "wrapper.h"

struct storage_ids {
	uint16_t seed;
	uint16_t pin;
};

extern const struct storage_ids storage_ids;
extern volatile bool storage_initd;

int storage_init();
bool storage_general_check(uint16_t id);
bool storage_general_write(const uint8_t *data, int len, uint16_t id);
int storage_general_read(uint8_t *data, size_t len, uint16_t id);
int storage_erase_zms();
int storage_erase_flash();

#endif
