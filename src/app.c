#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>
#include "gpio.h"
#include "app.h"
#include "storage.h"

bool app_csrand_get(void *dst, size_t len)
{
	sys_csrand_get(dst, len);
	return true;
}

void app_version_get(void *ver, size_t len)
{
	snprintf(ver, len, "0.4.0");
}

/**
 * @brief Get app supported features.
 *
 * @param [out] buffer buffer to fill with supported features.
 *
 * The buffer content represents:
 * - buffer[0]: Secure Boot
 * - buffer[1]: Flash Encryption
 * - buffer[2]: Bootloader
 * - buffer[3]: Storage
 * - buffer[4]: Hardware Rng support
 * - buffer[5]: Display & Input support
 * - buffer[6]: User Key support
 *
 * @return true if success, false error.
 *
 */
bool app_check_feature(uint8_t *buffer, size_t len)
{
	if (len < 5) {
		return false;
	}
	memset(buffer, 0, len);

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	buffer[2] = true;
#endif

#if defined(CONFIG_NVS) && defined(CONFIG_FLASH)
	buffer[3] = true;
#endif

#if defined(CONFIG_ENTROPY_DEVICE_RANDOM_GENERATOR) && defined(CONFIG_ENTROPY_HAS_DRIVER)
	buffer[4] = true;
#endif

#if defined(CONFIG_DISPLAY) && defined(CONFIG_INPUT) && defined(CONFIG_LVGL)
	buffer[5] = true;
#endif

#if defined(CONFIG_GPIO)
	if (user_button_exists()) {
		buffer[6] = true;
	}
#endif

	return true;
}

/**
 * @brief Get app runing status.
 *
 * @param [out] buffer buffer to fill with status.
 *
 * The buffer content represents:
 * - buffer[0]: Storage Init
 * - buffer[1]: Lock status
 *
 * @return true if success, false error.
 *
 */
bool app_check_status(uint8_t *buffer, size_t len)
{
	if (len < 3) {
		return false;
	}
	memset(buffer, 0, len);
	buffer[0] = storage_initd;
	buffer[1] = lock_mark_get();
	return true;
}

int app_device_euid_get(uint8_t *id, size_t len)
{
	if (len < 8) {
		return -1;
	}
	memset(id, 0, len);
	hwinfo_get_device_eui64(id);
	return 0;
}

bool app_check_lock()
{
	return lock_mark_get();
}

bool app_check_storage()
{
	return storage_initd;
}
