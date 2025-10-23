#include "storage.h"

#ifdef CONFIG_ZMS

#include <zephyr/kernel.h>
#include <zephyr/fs/zms.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>

#ifdef CONFIG_SOC_SERIES_ESP32C3

#include <esp_efuse.h>
#include <esp_efuse_table.h>

#endif

#define ZMS_PARTITION        storage_partition
#define ZMS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(ZMS_PARTITION)
#define ZMS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(ZMS_PARTITION)
#define ZMS_PARTITION_SIZE   FIXED_PARTITION_SIZE(ZMS_PARTITION)

static struct zms_fs fs;

bool ohw_official = false;

int storage_init()
{
	struct flash_pages_info info;

	fs.flash_device = ZMS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		return -ENODEV;
	}

	fs.offset = ZMS_PARTITION_OFFSET;
	int res = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (res < 0) {
		return res;
	}

	if (info.size == 0 || ZMS_PARTITION_SIZE < 1024) {
		return -EINVAL;
	}

	fs.sector_size = info.size;
	fs.sector_count = (ZMS_PARTITION_SIZE / fs.sector_size);

	// ZMS requires minimum 2 sectors
	if (fs.sector_count < 2) {
		return -EINVAL;
	}

	// printk("ZMS Configuration:\n");
	// printk("  Flash device: %p (%s)\n", fs.flash_device, fs.flash_device->name);
	// printk("  Offset: 0x%lx (%ld)\n", fs.offset, fs.offset);
	// printk("  Sector size: %d bytes\n", fs.sector_size);
	// printk("  Sector count: %d\n", fs.sector_count);
	// printk("  Total size: %d bytes\n", fs.sector_size * fs.sector_count);

	res = zms_mount(&fs);
	if (res < 0) {
		return res;
	}

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
	int res = zms_read(&fs, id, &read_value, sizeof(read_value));
	if (res < 0) {
		return false;
	}
	return true;
}

bool storage_general_write(const uint8_t *data, int len, uint16_t id)
{
	int res = zms_write(&fs, id, data, len);
	if (res < 0) {
		return false;
	}
	return true;
}

int storage_general_read(uint8_t *data, size_t len, uint16_t id)
{
	return zms_read(&fs, id, data, len);
}

int storage_erase_nvs()
{
	return zms_clear(&fs);
}

int storage_delete(uint16_t id)
{
	return zms_delete(&fs, id);
}

int storage_erase_flash()
{
	const struct flash_area *fa;

	int ret = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
	if (ret != 0) {
		return ret;
	}

	const struct device *flash_dev = flash_area_get_device(fa);

	ret = flash_erase(flash_dev, fa->fa_off, fa->fa_size);
	if (ret != 0) {
		return ret;
	}

	flash_area_close(fa);

	return ret;
}

#else

static uint8_t storage_seed_buffer[256] = {0};
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

int storage_erase_nvs()
{
	return 0;
}

int storage_delete(uint16_t id)
{
	return 0;
}

int storage_erase_flash()
{
	return 0;
}

#endif
