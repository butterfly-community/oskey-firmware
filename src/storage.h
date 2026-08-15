/* SPDX-License-Identifier: MPL-2.0 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct storage_ids {
	uint16_t seed;
	uint16_t unlock_failures;
	uint16_t firmware_update;
};

extern const struct storage_ids storage_ids;

int storage_init(void);
int storage_settings_load(void);
bool storage_ready(void);
int storage_exists(uint16_t id);
int storage_write(const uint8_t *data, size_t len, uint16_t id);
int storage_read(uint8_t *data, size_t len, uint16_t id);
int storage_delete(uint16_t id);
int storage_erase_flash(void);

#endif
