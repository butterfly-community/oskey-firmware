#include "storage.h"

uint8_t storage_fake_buffer[65] = {0};

int storage_seed_check()
{
	return (storage_fake_buffer[0] == 1);
}

int storage_seed_write_buffer(const uint8_t *data, int len)
{
	if (len > 64) {
		return -1;
	}

	storage_fake_buffer[0] = 1;
	memcpy(&storage_fake_buffer[1], data, len);
	return 0;
}

int storage_seed_read_buffer(uint8_t *data, int len)
{
	if (!storage_seed_check()) {
		return -1;
	}
	if (len > 64) {
		return -2;
	}
	memcpy(data, &storage_fake_buffer[1], len);
	return 0;
}
