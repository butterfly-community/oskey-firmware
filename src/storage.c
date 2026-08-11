#include "storage.h"

#include <errno.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "bus.h"

const struct storage_ids storage_ids = {
	.seed = 2,
	.unlock_failures = 3,
	.firmware_update = 4,
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

static void publish_storage_state(enum app_storage_state state)
{
	int ret = zbus_chan_pub(&app_storage_state_chan, &state, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish storage state: %d", ret);
	}
}

static int storage_init_result(int result)
{
	storage_initialized = result >= 0;
	enum app_storage_state state = result < 0 ? APP_STORAGE_ERROR : APP_STORAGE_READY;
	publish_storage_state(state);
	return result;
}

static int storage_runtime_result(int result)
{
	if (result < 0 && result != -ENOENT) {
		storage_initialized = false;
		publish_storage_state(APP_STORAGE_ERROR);
	}
	return result;
}

int storage_init(void)
{
	void *settings_storage;
	int res = settings_subsys_init();
	if (res < 0) {
		return storage_init_result(res);
	}

	res = settings_storage_get(&settings_storage);
	if (res < 0) {
		return storage_init_result(res);
	}

	if (settings_storage == NULL) {
		return storage_init_result(-ENODEV);
	}
	fs = settings_storage;

	LOG_INF("ZMS device=%p (%s), offset=0x%lx, sector=%u x %u, total=%u bytes",
		(const void *)fs->flash_device, fs->flash_device->name, (unsigned long)fs->offset,
		fs->sector_size, fs->sector_count, fs->sector_size * fs->sector_count);

	return storage_init_result(0);
}

int storage_settings_load(void)
{
	return storage_runtime_result(settings_load());
}

int storage_exists(uint16_t id)
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

int storage_write(const uint8_t *data, size_t len, uint16_t id)
{
	if (!storage_initialized) {
		return -ENODEV;
	}
	if (data == NULL || len == 0) {
		return -EINVAL;
	}

	ssize_t res = zms_write(fs, id, data, len);
	if (res < 0) {
		return storage_runtime_result((int)res);
	}
	return 0;
}

int storage_read(uint8_t *data, size_t len, uint16_t id)
{
	if (!storage_initialized) {
		return -ENODEV;
	}
	if (data == NULL || len == 0) {
		return -EINVAL;
	}

	ssize_t stored_len = zms_get_data_length(fs, id);
	if (stored_len < 0) {
		return storage_runtime_result((int)stored_len);
	}
	if ((size_t)stored_len > len) {
		return -EMSGSIZE;
	}

	return storage_runtime_result((int)zms_read(fs, id, data, (size_t)stored_len));
}

int storage_delete(uint16_t id)
{
	if (!storage_initialized) {
		return -ENODEV;
	}

	return storage_runtime_result(zms_delete(fs, id));
}

int storage_erase_flash(void)
{
	if (fs == NULL) {
		return -ENODEV;
	}

	int ret = zms_clear(fs);
	storage_initialized = false;
	if (ret < 0) {
		publish_storage_state(APP_STORAGE_ERROR);
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

int storage_exists(uint16_t id)
{
	if (id == storage_ids.seed) {
		return storage_seed_len > 0;
	}
	if (id == storage_ids.unlock_failures) {
		return storage_unlock_failures > 0;
	}
	return false;
}

int storage_write(const uint8_t *data, size_t len, uint16_t id)
{
	if (data == NULL || len == 0) {
		return -EINVAL;
	}
	if (id == storage_ids.seed) {
		if (len > sizeof(storage_seed_buffer)) {
			return -EMSGSIZE;
		}
		memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
		memcpy(storage_seed_buffer, data, len);
		storage_seed_len = len;
		return 0;
	}
	if (id == storage_ids.unlock_failures && len == sizeof(storage_unlock_failures)) {
		storage_unlock_failures = *data;
		return 0;
	}
	return -EINVAL;
}

int storage_read(uint8_t *data, size_t len, uint16_t id)
{
	if (data == NULL || len == 0) {
		return -EINVAL;
	}
	if (id == storage_ids.seed) {
		if (storage_seed_len == 0) {
			return -ENOENT;
		}
		if (len < storage_seed_len) {
			return -EMSGSIZE;
		}
		memcpy(data, storage_seed_buffer, storage_seed_len);
		return storage_seed_len;
	}
	if (id == storage_ids.unlock_failures) {
		*data = storage_unlock_failures;
		return sizeof(storage_unlock_failures);
	}
	return -ENOENT;
}

int storage_delete(uint16_t id)
{
	if (id == storage_ids.seed) {
		memset(storage_seed_buffer, 0, sizeof(storage_seed_buffer));
		storage_seed_len = 0;
		return 0;
	}
	if (id == storage_ids.unlock_failures) {
		storage_unlock_failures = 0;
		return 0;
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
