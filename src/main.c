#include <zephyr/drivers/uart.h>
#include <zephyr/settings/settings.h>
#include <zephyr/device.h>
#include "wrapper.h"
#include "uart.h"
#include "bluetooth/bluetooth.h"
#include "storage.h"
#include "boot.h"
#include "app.h"
#include "net/wifi.h"
#include "net/mqtt.h"
#include "display/lvgl.h"
#include "gpio.h"

LOG_MODULE_REGISTER(main);
#include "usb/webusb.h"

int main(void)
{
	storage_init();

	user_button_init();

	oskey_bt_init();

	if (IS_ENABLED(CONFIG_OSKEY_STORAGE) && app_check_storage()) {
		settings_load();
	}

	int ret = app_init_display();
	if (ret < 0) {
		LOG_ERR("Display startup failed: %d", ret);
	}

	ret = init_usb_stack();
	if (ret < 0) {
		LOG_ERR("USB startup failed: %d", ret);
	}

	app_uart_irq_register();

	oskey_bt_start();

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
		confirm_mcuboot_img();
	}

	return 0;
}

void rust_panic_wrap(void)
{
	k_panic();
}
