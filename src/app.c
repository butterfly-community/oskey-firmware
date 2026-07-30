#include <stdio.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/reboot.h>
#include "gpio.h"
#include "app.h"
#include "storage.h"

bool app_csrand_get(void *dst, size_t len)
{
	return sys_csrand_get(dst, len) == 0;
}

void app_version_get(void *ver, size_t len)
{
	snprintf(ver, len, "0.7.1");
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
	if (len < 7) {
		return false;
	}
	memset(buffer, 0, len);

#if defined(CONFIG_OSKEY_MCUBOOT)
	buffer[2] = true;
#endif

#if defined(CONFIG_OSKEY_STORAGE)
	buffer[3] = true;
#endif

#if defined(CONFIG_ENTROPY_DEVICE_RANDOM_GENERATOR) && defined(CONFIG_ENTROPY_HAS_DRIVER)
	buffer[4] = true;
#endif

#if defined(CONFIG_OSKEY_DISPLAY)
	buffer[5] = true;
#endif

#if defined(CONFIG_GPIO)
	if (user_button_exists()) {
		buffer[6] = true;
	}
#endif

	return true;
}

void app_get_chip_model(char *buffer, size_t len)
{
	snprintf(buffer, len, "%s", CONFIG_SOC);
}

int app_get_eui64(uint8_t *buffer, size_t len)
{
	return hwinfo_get_device_eui64(buffer);
}

int app_get_device_id(uint8_t *buffer, size_t len)
{
	return hwinfo_get_device_id(buffer, len);
}

bool app_check_storage(void)
{
	return storage_initd;
}

void app_storage_reset(void)
{
	if (app_check_storage()) {
		storage_erase_zms();
	}
	storage_erase_flash();
	sys_reboot(SYS_REBOOT_COLD);
}

void app_restart(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}
