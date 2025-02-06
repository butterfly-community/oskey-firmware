#include "storage.h"

#ifdef CONFIG_NVS

#include <zephyr/kernel.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/device.h>

#define STORAGE_NODE DT_NODE_BY_FIXED_PARTITION_LABEL(storage)
#define FLASH_NODE   DT_MTD_FROM_FIXED_PARTITION(STORAGE_NODE)

static struct nvs_fs fs = {
	.flash_device = DEVICE_DT_GET(FLASH_NODE),
	.offset = DT_REG_ADDR(STORAGE_NODE),
	.sector_size = 4096,
	.sector_count = DT_REG_SIZE(STORAGE_NODE) / 4096,
};

int storage_init()
{
	int res = device_is_ready(fs.flash_device);
	if (res < 0) {
		return res;
	}

	res = nvs_mount(&fs);
	if (res < 0) {
		return res;
	}
	return res;
}

bool storage_seed_check()
{
	uint8_t read_value[65] = {0};
	int res = nvs_read(&fs, 2, &read_value, sizeof(read_value));
	if (res < 0) {
		return false;
	}
	return true;
}

int storage_seed_write_buffer(const uint8_t *data, int len)
{
	int res = nvs_write(&fs, 2, data, len);
	if (res < 0) {
		return res;
	}
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
	int res = nvs_read(&fs, 2, data, len);
	if (res < 0) {
		return res;
	}
	return 0;
}

#else

uint8_t storage_fake_buffer[65] = {0};

int storage_init()
{
	return 0;
}

bool storage_seed_check()
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
#endif
