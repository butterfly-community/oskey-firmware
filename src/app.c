#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/random/random.h>
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
	snprintf(ver, len, "0.7.1");
}

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

#if defined(CONFIG_ENTROPY_DEVICE_RANDOM_GENERATOR) && defined(CONFIG_ENTROPY_HAS_DRIVER)
	buffer[APP_FEATURE_HARDWARE_RNG] = true;
#endif

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

int app_fido_pin_info_get(bool *is_set, uint8_t *retries)
{
#if defined(CONFIG_OSKEY_FIDO2)
	uint8_t pin_hash[FIDO2_PIN_HASH_SIZE];
	int ret;

	if (is_set == NULL || retries == NULL) {
		return -EINVAL;
	}

	ret = fido2_storage_pin_get(pin_hash);
	memset(pin_hash, 0, sizeof(pin_hash));
	*is_set = ret == 0;
	*retries = 0;
	if (ret == -ENOENT) {
		return 0;
	}
	return ret == 0 ? fido2_storage_pin_retries_get(retries) : ret;
#else
	ARG_UNUSED(is_set);
	ARG_UNUSED(retries);
	return -ENOTSUP;
#endif
}

void app_fido_pin_recover(void)
{
#if defined(CONFIG_OSKEY_FIDO2)
	bool pin_set;
	uint8_t retries;

	if (app_fido_pin_info_get(&pin_set, &retries) == 0 && pin_set && retries == 0) {
		(void)fido2_storage_pin_retries_reset();
	}
#endif
}

void app_restart(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}
