#include <zephyr/random/random.h>
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
	snprintf(ver, len, "0.0.3");
}

bool app_check_support(uint32_t number)
{
	if (number == CHECK_INPUT_DISPLAY) {
#if defined(CONFIG_DISPLAY) && defined(CONFIG_INPUT)
		return true;
#else
		return false;
#endif
	}
	return false;
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
 * - buffer[3]: Storage Init
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
  if (storage_initd) {
	  buffer[3] = true;
	}
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

bool app_check_lock()
{
	return lock_mark;
}

bool app_check_storage()
{
	return storage_initd;
}