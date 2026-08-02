#include "storage.h"

#include <errno.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "bus.h"

const struct storage_ids storage_ids = {
	.seed = 2,
	.unlock_failures = 3,
};

static bool storage_initialized;

bool storage_ready(void)
{
	return storage_initialized;
}

#ifdef CONFIG_OSKEY_STORAGE

#include <zephyr/kvss/zms.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(oskey_storage);

BUILD_ASSERT(IS_ENABLED(CONFIG_SETTINGS_ZMS), "OSKey storage requires the ZMS settings backend");

static struct zms_fs *fs;

static int storage_result(int result)
{
	storage_initialized = result >= 0;
	enum app_storage_state state = result < 0 ? APP_STORAGE_ERROR : APP_STORAGE_READY;
	app_storage_state_publish(state);
	return result;
}

static int storage_runtime_result(int result)
{
	if (result < 0 && result != -ENOENT) {
		storage_initialized = false;
		app_storage_state_publish(APP_STORAGE_ERROR);
	}
	return result;
}

int storage_init(void)
{
	void *settings_storage;
	int res = settings_subsys_init();
	if (res < 0) {
		return storage_result(res);
	}

	res = settings_storage_get(&settings_storage);
	if (res < 0) {
		return storage_result(res);
	}

	if (settings_storage == NULL) {
		return storage_result(-ENODEV);
	}
	fs = settings_storage;

	LOG_INF("ZMS device=%p (%s), offset=0x%lx, sector=%u x %u, total=%u bytes",
		(const void *)fs->flash_device, fs->flash_device->name, (unsigned long)fs->offset,
		fs->sector_size, fs->sector_count, fs->sector_size * fs->sector_count);

	return storage_result(0);
}

int storage_settings_load(void)
{
	return storage_runtime_result(settings_load());
}

int storage_general_check(uint16_t id)
{
	if (!storage_initialized) {
		return -ENODEV;
	}

	ssize_t res = zms_get_data_length(fs, id);
	if (res >= 0) {
		return res > 0;
	}
	return res == -ENOENT ? 0 : storage_runtime_result((int)res);
}

bool storage_general_write(const uint8_t *data, size_t len, uint16_t id)
{
	if (!storage_initialized) {
		return false;
	}

	int res = zms_write(fs, id, data, len);
	if (res < 0) {
		storage_runtime_result(res);
		return false;
	}
	return true;
}

int storage_general_read(uint8_t *data, size_t len, uint16_t id)
{
	if (!storage_initialized) {
		return -ENODEV;
	}

	return storage_runtime_result(zms_read(fs, id, data, len));
}

int storage_erase_flash(void)
{
	if (fs == NULL) {
		return -ENODEV;
	}

	int ret = zms_clear(fs);
	storage_initialized = false;
	if (ret < 0) {
		app_storage_state_publish(APP_STORAGE_ERROR);
	}
	return ret;
}

#else

static uint8_t storage_seed_buffer[256] = {0};
static size_t storage_seed_len;
static uint8_t storage_unlock_failures;

int storage_init(void)
{
	memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
	storage_seed_len = 0;
	storage_unlock_failures = 0;
	return 0;
}

int storage_settings_load(void)
{
	return 0;
}

int storage_general_check(uint16_t id)
{
	if (id == storage_ids.seed) {
		return storage_seed_len > 0;
	}
	if (id == storage_ids.unlock_failures) {
		return true;
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
	if (id == storage_ids.unlock_failures && len == sizeof(storage_unlock_failures)) {
		storage_unlock_failures = *data;
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
	if (id == storage_ids.unlock_failures && len > 0) {
		*data = storage_unlock_failures;
		return sizeof(storage_unlock_failures);
	}
	return -ENOENT;
}

int storage_erase_flash(void)
{
	memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
	storage_seed_len = 0;
	storage_unlock_failures = 0;
	return 0;
}

#endif
