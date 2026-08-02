#ifndef OSKEY_FIDO2_PIN_H
#define OSKEY_FIDO2_PIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct oskey_fido_pin_info {
	bool set;
	uint8_t retries;
};

int oskey_fido_pin_info_get(struct oskey_fido_pin_info *info);
int oskey_fido_pin_set(const char *pin, size_t len);

#endif
