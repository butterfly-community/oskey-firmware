#include "storage.h"

#include <zephyr/kernel.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/device.h>

#ifdef CONFIG_SOC_SERIES_ESP32C3

#include <esp_efuse.h>
#include <esp_efuse_table.h>

#endif

#define NVS_PARTITION_NODE DT_NODE_BY_FIXED_PARTITION_LABEL(storage)
#define NVS_FLASH_NODE     DT_MTD_FROM_FIXED_PARTITION(NVS_PARTITION_NODE)

#define NVS_FLASH_ERASE_SIZE DT_PROP(DT_GPARENT(NVS_PARTITION_NODE), erase_block_size)
#define NVS_PARTITION_OFFSET DT_REG_ADDR(NVS_PARTITION_NODE)
#define NVS_PARTITION_SIZE   DT_REG_SIZE(NVS_PARTITION_NODE)
#define NVS_SECTOR_COUNT     (NVS_PARTITION_SIZE / NVS_FLASH_ERASE_SIZE)

static struct nvs_fs fs = {
	.flash_device = DEVICE_DT_GET(NVS_FLASH_NODE),
	.offset = NVS_PARTITION_OFFSET,
	.sector_size = NVS_FLASH_ERASE_SIZE,
	.sector_count = NVS_SECTOR_COUNT,
};

bool ohw_official = false;

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

	printk("NVS Configuration:\n");
	printk("  Flash device: %p (%s)\n", fs.flash_device, fs.flash_device->name);
	printk("  Offset: 0x%lx (%ld)\n", fs.offset, fs.offset);
	printk("  Sector size: %d bytes\n", fs.sector_size);
	printk("  Sector count: %d\n", fs.sector_count);
	printk("  Total size: %d bytes\n", fs.sector_size * fs.sector_count);

#ifdef CONFIG_SOC_SERIES_ESP32C3

	uint8_t ohw_data[32] = {0};
	esp_err_t err = esp_efuse_read_block(EFUSE_BLK6, ohw_data, 0, 256);

	if (err == ESP_OK && ohw_data[31] == 0xFF) {
		ohw_official = true;
	}

#endif
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

int storage_erase()
{
	int res = nvs_clear(&fs);

	if (res < 0) {
		return res;
	}

	return 0;
}

int storage_delete(uint16_t id)
{
	int res = nvs_delete(&fs, id);

	if (res < 0) {
		return res;
	}

	return 0;
}
