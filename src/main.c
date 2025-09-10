#include <zephyr/random/random.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/settings/settings.h>
#include <zephyr/device.h>
#include "wrapper.h"
#include "uart.h"
#include "bluetooth.h"
#include "storage.h"
#include "boot.h"
#include "net/wifi.h"
#include "net/mqtt.h"
#include "display/lvgl.h"

int main(void)
{
	k_msleep(1000);

	storage_init();

	k_work_init(&app_uart_work, app_uart_work_handler);

	app_uart_irq_register();

	bt_init();

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	bt_start();

	wifi_start();

	if (IS_ENABLED(CONFIG_MQTT)) {
		k_msleep(10000);
		mqtt_start();
	}

	app_init_display();

	confirm_mcuboot_img();

	app_display_loop();

	return 0;
}

bool app_cs_random(void *dst, size_t len)
{
	sys_csrand_get(dst, len);
	return true;
}

void rust_panic_wrap(void)
{
	k_panic();
}

void app_version_get(void *ver, size_t len)
{
	snprintf(ver, len, "0.0.3");
}

bool app_check_support(uint32_t number)
{
	if (number == CHECK_INPUT_DISPLAY) {
#if defined(CONFIG_DISPLAY) && defined(CONFIG_INPUT)
		return true;
#else
		return false;
#endif
	}
	return false;
}

bool app_check_lock(uint32_t number)
{
	return lock_mark;
}
