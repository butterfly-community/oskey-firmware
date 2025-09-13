#include "storage.h"

#ifdef CONFIG_NVS

#include <zephyr/kernel.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>

#ifdef CONFIG_SOC_SERIES_ESP32C3

#include <esp_efuse.h>
#include <esp_efuse_table.h>

#endif

#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   FIXED_PARTITION_SIZE(NVS_PARTITION)

static struct nvs_fs fs;

bool ohw_official = false;

int storage_init()
{
	struct flash_pages_info info;

	fs.flash_device = NVS_PARTITION_DEVICE;
	int res = device_is_ready(fs.flash_device);
	if (res < 0) {
		return res;
	}

	fs.offset = NVS_PARTITION_OFFSET;
	res = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (res < 0) {
		return res;
	}

	fs.sector_size = info.size;
	fs.sector_count = (NVS_PARTITION_SIZE / fs.sector_size);
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
	if (res >= 0) {
		storage_initd = true;
	}
	return res;
}

bool storage_general_check(uint16_t id)
{
	uint8_t read_value[2] = {0};
	int res = nvs_read(&fs, id, &read_value, sizeof(read_value));
	if (res < 0) {
		return false;
	}
	return true;
}

bool storage_general_write(const uint8_t *data, int len, uint16_t id)
{
	int res = nvs_write(&fs, id, data, len);
	if (res < 0) {
		return false;
	}
	return true;
}

int storage_general_read(uint8_t *data, size_t len, uint16_t id)
{
	return nvs_read(&fs, id, data, len);
}

int storage_erase()
{
	return nvs_clear(&fs);
}

int storage_delete(uint16_t id)
{
	return nvs_delete(&fs, id);
}

void storage_erase_partition()
{
	const struct flash_area *fa;

	int ret = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
	if (ret != 0) {
		printk("Failed to open flash area: %d\n", ret);
		return;
	}

	const struct device *flash_dev = flash_area_get_device(fa);

	ret = flash_erase(flash_dev, fa->fa_off, fa->fa_size);
	if (ret != 0) {
		printk("Flash erase failed: %d\n", ret);
	} else {
		printk("Storage partition erased successfully.\n");
	}

	flash_area_close(fa);
}

#else

static uint8_t storage_seed_buffer[64] = {0};
static uint8_t storage_pin_buffer[22] = {0};

int storage_init()
{
	memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
	memset(storage_pin_buffer, 0, sizeof(storage_pin_buffer));
	return 0;
}

bool storage_general_check(uint16_t id)
{
	if (id == STORAGE_ID_SEED) {
		return storage_seed_buffer[0] != 0;
	} else if (id == STORAGE_ID_PIN) {
		return storage_pin_buffer[0] != 0;
	}
	return false;
}

bool storage_general_write(const uint8_t *data, int len, uint16_t id)
{
	if (id == STORAGE_ID_SEED) {
		if (len > sizeof(storage_seed_buffer)) {
			return false;
		}
		memcpy(storage_seed_buffer, data, len);
		return true;
	} else if (id == STORAGE_ID_PIN) {
		if (len > sizeof(storage_pin_buffer)) {
			return false;
		}
		memcpy(storage_pin_buffer, data, len);
		return true;
	}
	return false;
}

int storage_general_read(uint8_t *data, size_t len, uint16_t id)
{
	if (id == STORAGE_ID_SEED) {
		if (len > sizeof(storage_seed_buffer)) {
			len = sizeof(storage_seed_buffer);
		}
		memcpy(data, storage_seed_buffer, len);
		return len;
	} else if (id == STORAGE_ID_PIN) {
		if (len > sizeof(storage_pin_buffer)) {
			len = sizeof(storage_pin_buffer);
		}
		memcpy(data, storage_pin_buffer, len);
		return len;
	}
	return false;
}

int storage_erase()
{
	return 0;
}

int storage_delete(uint16_t id)
{
	return 0;
}
#endif
