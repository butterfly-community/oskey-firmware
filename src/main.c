/* SPDX-License-Identifier: MPL-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "uart.h"
#include "bluetooth/bluetooth.h"
#include "storage.h"
#include "boot.h"
#include "app.h"
#include "net/wifi.h"
#include "net/mqtt.h"
#include "display/display.h"
#if defined(CONFIG_OSKEY_CAMERA)
#include "camera/camera.h"
#endif
#if defined(CONFIG_OSKEY_AUDIO)
#include "audio/audio.h"
#endif
#if defined(CONFIG_OSKEY_IMU)
#include "imu/imu.h"
#endif
#include "bus.h"
#include "core.h"
#include "gpio.h"
#include "usb/webusb.h"
LOG_MODULE_REGISTER(main);

int main(void)
{
	bool healthy = true;
	int ret;

	if (IS_ENABLED(CONFIG_OSKEY_TEST_FIRMWARE)) {
		LOG_WRN("Test firmware enabled; confirmations are automatic");
	}

#if defined(CONFIG_OSKEY_AUDIO)
	ret = app_audio_init();
	if (ret < 0) {
		LOG_ERR("Audio init failed: %d", ret);
	}
#endif

#if defined(CONFIG_OSKEY_IMU)
	ret = app_imu_init();
	if (ret < 0) {
		LOG_ERR("IMU init failed: %d", ret);
	}
#endif

	ret = storage_init();
	if (ret < 0) {
		LOG_ERR("Storage startup failed: %d", ret);
		healthy = false;
	}
#ifdef CONFIG_OSKEY_MCUBOOT
	if (app_update_mode_take()) {
		return 0;
	}
#endif

	ret = user_button_init();
	if (ret < 0 && ret != -ENOTSUP) {
		LOG_ERR("User button startup failed: %d", ret);
		healthy = false;
	}

	int bluetooth_status = oskey_bt_init();
	if (bluetooth_status < 0) {
		healthy = false;
	}

	if (IS_ENABLED(CONFIG_OSKEY_STORAGE) && app_check_storage()) {
		ret = storage_settings_load();
		if (ret < 0) {
			LOG_ERR("Settings load failed: %d", ret);
			healthy = false;
		}
	}

	int core_status = app_core_init();
	if (core_status < 0) {
		LOG_ERR("Core startup failed: %d", core_status);
		healthy = false;
	}

#if defined(CONFIG_OSKEY_CAMERA)
	ret = app_camera_init();
	if (ret < 0) {
		LOG_ERR("Camera startup failed: %d", ret);
	}
#endif

	ret = app_init_display();
	if (ret < 0) {
		LOG_ERR("Display startup failed: %d", ret);
		healthy = false;
	}

	ret = init_usb_stack();
	if (ret < 0) {
		LOG_ERR("USB startup failed: %d", ret);
		healthy = false;
	}

	if (IS_ENABLED(CONFIG_OSKEY_RUST) && core_status == 0) {
		ret = app_uart_irq_register();
		if (ret < 0) {
			LOG_ERR("UART startup failed: %d", ret);
			healthy = false;
		}
	}

	if (bluetooth_status == 0) {
		ret = oskey_bt_start();
		if (ret < 0) {
			LOG_ERR("Bluetooth startup failed: %d", ret);
			healthy = false;
		}
	}

	ret = wifi_start();

	if (ret < 0) {
		LOG_ERR("Wi-Fi startup failed: %d", ret);
		healthy = false;
	}

	if (IS_ENABLED(CONFIG_OSKEY_MQTT)) {
		ret = mqtt_start();
		if (ret < 0) {
			LOG_ERR("MQTT startup failed: %d", ret);
			healthy = false;
		}
	}

#ifdef CONFIG_OSKEY_MCUBOOT
	if (healthy) {
		ret = confirm_mcuboot_img();
		if (ret < 0) {
			LOG_ERR("Image confirmation failed: %d", ret);
		}
	}
#endif

	return 0;
}

void rust_panic_wrap(void)
{
	k_panic();
}
