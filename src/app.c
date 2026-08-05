#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include "gpio.h"
#include "app.h"
#include "display/display.h"
#include "storage.h"

#if defined(CONFIG_OSKEY_FIDO2)
#include <zephyr/authentication/fido2/fido2_storage.h>
#endif

bool app_csrand_get(void *dst, size_t len)
{
	return sys_csrand_get(dst, len) == 0;
}

void app_version_get(void *ver, size_t len)
{
	snprintf(ver, len, "%s", APP_VERSION_STRING);
}

#ifndef CONFIG_OSKEY_MCUBOOT
bool app_update_request(void)
{
	return false;
}
#endif

bool app_check_feature(uint8_t *buffer, size_t len)
{
	if (buffer == NULL || len < APP_FEATURE_COUNT) {
		return false;
	}
	memset(buffer, 0, len);

#if defined(CONFIG_SECURE_BOOT)
	buffer[APP_FEATURE_SECURE_BOOT] = true;
#endif

#if defined(CONFIG_ESP_FLASH_ENCRYPTION)
	buffer[APP_FEATURE_FLASH_ENCRYPTION] = true;
#endif

#if defined(CONFIG_OSKEY_MCUBOOT)
	buffer[APP_FEATURE_BOOTLOADER] = true;
#endif

#if defined(CONFIG_OSKEY_STORAGE)
	buffer[APP_FEATURE_STORAGE] = true;
#endif

	buffer[APP_FEATURE_HARDWARE_RNG] = IS_ENABLED(CONFIG_CSPRNG_ENABLED) &&
					   !IS_ENABLED(CONFIG_TEST_RANDOM_GENERATOR) &&
					   !IS_ENABLED(CONFIG_FAKE_ENTROPY_NATIVE_SIM);

	buffer[APP_FEATURE_DISPLAY_INPUT] = app_display_ready();

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

bool app_storage_reset(void)
{
	if (storage_erase_flash() < 0) {
		return false;
	}
	sys_reboot(SYS_REBOOT_COLD);
	return true;
}

int app_fido_pin_retries(void)
{
#if defined(CONFIG_OSKEY_FIDO2)
	uint8_t pin_hash[FIDO2_PIN_HASH_SIZE];
	uint8_t retries;
	int ret;

	ret = fido2_storage_pin_get(pin_hash);
	memset(pin_hash, 0, sizeof(pin_hash));
	if (ret < 0) {
		return ret;
	}

	ret = fido2_storage_pin_retries_get(&retries);
	return ret < 0 ? ret : retries;
#else
	return -ENOTSUP;
#endif
}

void app_fido_pin_recover(void)
{
#if defined(CONFIG_OSKEY_FIDO2)
	if (app_fido_pin_retries() == 0) {
		(void)fido2_storage_pin_retries_reset();
	}
#endif
}

void app_restart(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}
