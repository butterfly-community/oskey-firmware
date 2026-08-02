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
#include "bus.h"
#include "core.h"
#include "gpio.h"
#include "transport.h"
#include "usb/webusb.h"

LOG_MODULE_REGISTER(main);

int main(void)
{
	int ret = storage_init();
	if (ret < 0) {
		LOG_ERR("Storage startup failed: %d", ret);
	}

	ret = user_button_init();
	if (ret < 0 && ret != -ENOTSUP) {
		LOG_ERR("User button startup failed: %d", ret);
	}

	int bluetooth_status = oskey_bt_init();

	if (IS_ENABLED(CONFIG_OSKEY_STORAGE) && app_check_storage()) {
		ret = storage_settings_load();
		if (ret < 0) {
			LOG_ERR("Settings load failed: %d", ret);
		}
	}

	int core_status = app_core_init();
	if (core_status < 0) {
		LOG_ERR("Core startup failed: %d", core_status);
	}

	if (core_status == 0) {
		app_transport_init();
	}

	ret = app_init_display();
	if (ret < 0) {
		LOG_ERR("Display startup failed: %d", ret);
	}

	ret = init_usb_stack();
	if (ret < 0) {
		LOG_ERR("USB startup failed: %d", ret);
	}

	if (IS_ENABLED(CONFIG_OSKEY_RUST) && core_status == 0) {
		ret = app_uart_irq_register();
		if (ret < 0) {
			LOG_ERR("UART startup failed: %d", ret);
		}
	}

	if (bluetooth_status == 0) {
		ret = oskey_bt_start();
		if (ret < 0) {
			LOG_ERR("Bluetooth startup failed: %d", ret);
		}
	}

	ret = wifi_start();

	if (ret < 0) {
		LOG_ERR("Wi-Fi startup failed: %d", ret);
	}

	if (IS_ENABLED(CONFIG_OSKEY_MQTT)) {
		ret = mqtt_start();
		if (ret < 0) {
			LOG_ERR("MQTT startup failed: %d", ret);
		}
	}

	if (IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT)) {
		ret = confirm_mcuboot_img();
		if (ret < 0) {
			LOG_ERR("Image confirmation failed: %d", ret);
		}
	}

	return 0;
}

void rust_panic_wrap(void)
{
	k_panic();
}
