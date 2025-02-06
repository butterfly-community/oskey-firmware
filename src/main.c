#include <zephyr/random/random.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/settings/settings.h>
#include <zephyr/device.h>
#include "wrapper.h"
#include "uart.h"
#include "bluetooth.h"
#include "storage.h"
#include "boot.h"
#include "display/screen.h"

void cs_random(void *dst, size_t len);

int main(void)
{
	printk("build time: " __DATE__ " " __TIME__ "\n");

	storage_init();

	storage_erase();

	k_work_init(&app_uart_work, app_uart_work_handler);

	app_uart_irq_register();

	bt_init();

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	bt_start();

	confirm_mcuboot_img();

	test_lvgl();

	app_display_loop();

	return 0;
}

void cs_random(void *dst, size_t len)
{
	sys_csrand_get(dst, len);
}

void rust_panic_wrap(void)
{
	k_panic();
}
