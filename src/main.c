#include <zephyr/drivers/uart.h>
#include <zephyr/settings/settings.h>
#include <zephyr/device.h>
#include "wrapper.h"
#include "uart.h"
#include "bluetooth.h"
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

	app_sign_trigger();

	app_uart_irq_register();

	user_button_init();

	bt_init();

	if (IS_ENABLED(CONFIG_SETTINGS) && app_check_storage()) {
		settings_load();
	}

	bt_start();

	wifi_start();

	if (IS_ENABLED(CONFIG_MQTT)) {
		k_msleep(10000);
		mqtt_start();
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
