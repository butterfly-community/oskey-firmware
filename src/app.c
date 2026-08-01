#include <errno.h>
#include <stdio.h>
#include <string.h>
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

bool app_check_feature(uint8_t *buffer, size_t len)
{
	if (buffer == NULL || len < APP_FEATURE_COUNT) {
		return false;
	}
	memset(buffer, 0, len);

#if defined(CONFIG_OSKEY_MCUBOOT)
	buffer[APP_FEATURE_BOOTLOADER] = true;
#endif

#if defined(CONFIG_OSKEY_STORAGE)
	buffer[APP_FEATURE_STORAGE] = true;
#endif

#if defined(CONFIG_ENTROPY_DEVICE_RANDOM_GENERATOR) && defined(CONFIG_ENTROPY_HAS_DRIVER)
	buffer[APP_FEATURE_HARDWARE_RNG] = true;
#endif

#if defined(CONFIG_OSKEY_DISPLAY)
	buffer[APP_FEATURE_DISPLAY_INPUT] = true;
#endif

#if defined(CONFIG_GPIO)
	if (user_button_exists()) {
		buffer[APP_FEATURE_USER_BUTTON] = true;
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
	if (buffer == NULL || len < sizeof(uint64_t)) {
		return -EINVAL;
	}

	return hwinfo_get_device_eui64(buffer);
}

int app_get_device_id(uint8_t *buffer, size_t len)
{
	return hwinfo_get_device_id(buffer, len);
}

bool app_check_storage(void)
{
	return storage_ready();
}

void app_storage_reset(void)
{
	storage_erase_flash();
	sys_reboot(SYS_REBOOT_COLD);
}

void app_restart(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}
