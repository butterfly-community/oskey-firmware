#include "storage.h"

#include "wrapper.h"

const struct storage_ids storage_ids = {
	.seed = 2,
};

volatile bool storage_initd;

#ifdef CONFIG_OSKEY_STORAGE

#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/zms.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(oskey_storage);

static struct zms_fs *fs;

int storage_init(void)
{
	void *settings_storage;
	int res = settings_subsys_init();
	if (res < 0) {
		return res;
	}

	res = settings_storage_get(&settings_storage);
	if (res < 0) {
		return res;
	}

	fs = settings_storage;

	LOG_INF("ZMS device=%p (%s), offset=0x%lx, sector=%u x %u, total=%u bytes",
		(const void *)fs->flash_device, fs->flash_device->name, (unsigned long)fs->offset,
		fs->sector_size, fs->sector_count, fs->sector_size * fs->sector_count);

	storage_initd = true;
	return 0;
}

bool storage_general_check(uint16_t id)
{
	uint8_t read_value[2] = {0};
	int res = zms_read(fs, id, &read_value, sizeof(read_value));
	if (res < 0) {
		return false;
	}
	return true;
}

bool storage_general_write(const uint8_t *data, size_t len, uint16_t id)
{
	int res = zms_write(fs, id, data, len);
	if (res < 0) {
		return false;
	}
	return true;
}

int storage_general_read(uint8_t *data, size_t len, uint16_t id)
{
	return zms_read(fs, id, data, len);
}

int storage_erase_zms(void)
{
	return zms_clear(fs);
}

int storage_delete(uint16_t id)
{
	return zms_delete(fs, id);
}

int storage_erase_flash(void)
{
	const struct flash_area *fa;

	int ret = flash_area_open(PARTITION_ID(storage_partition), &fa);
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
static size_t storage_seed_len;

int storage_init(void)
{
	memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
	storage_seed_len = 0;
	return 0;
}

bool storage_general_check(uint16_t id)
{
	if (id == storage_ids.seed) {
		return storage_seed_len > 0;
	}
	return false;
}

bool storage_general_write(const uint8_t *data, size_t len, uint16_t id)
{
	if (id == storage_ids.seed) {
		if (len > sizeof(storage_seed_buffer)) {
			return false;
		}
		memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
		memcpy(storage_seed_buffer, data, len);
		storage_seed_len = len;
		return true;
	}
	return false;
}

int storage_general_read(uint8_t *data, size_t len, uint16_t id)
{
	if (id == storage_ids.seed) {
		size_t read_len = MIN(len, storage_seed_len);
		memcpy(data, storage_seed_buffer, read_len);
		return read_len;
	}
	return -ENOENT;
}

int storage_erase_zms(void)
{
	memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
	storage_seed_len = 0;
	return 0;
}

int storage_delete(uint16_t id)
{
	return 0;
}

int storage_erase_flash(void)
{
	memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
	storage_seed_len = 0;
	return 0;
}

#endif
