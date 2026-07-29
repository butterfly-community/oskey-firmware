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
	init_usb_stack();

	storage_init();

	app_uart_irq_register();

	user_button_init();

	oskey_bt_init();

	if (IS_ENABLED(CONFIG_OSKEY_STORAGE) && app_check_storage()) {
		settings_load();
	}

	oskey_bt_start();

	int ret = wifi_start();

	if (ret < 0) {
		LOG_ERR("Wi-Fi startup failed: %d", ret);
	}

	if (IS_ENABLED(CONFIG_OSKEY_MQTT)) {
		ret = mqtt_start();
		if (ret < 0) {
			LOG_ERR("MQTT startup failed: %d", ret);
		}
	}

	app_init_display();

	if (IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_DIRECT_XIP_WITH_REVERT)) {
		confirm_mcuboot_img();
	}

	app_display_loop();

	return 0;
}

void rust_panic_wrap(void)
{
	k_panic();
}
