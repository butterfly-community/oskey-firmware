#ifdef CONFIG_DISPLAY

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <lvgl.h>
#include <lv_demos.h>
#include "wrapper.h"
#include "screen.h"

const struct device *display_dev;

int test_lvgl()
{
	const struct device *display_dev;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		// LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	lv_demo_benchmark();

	lv_task_handler();
	display_blanking_off(display_dev);

	while (1) {
		k_msleep(lv_task_handler());
	}

	return 0;
}

int app_display_bt_pin(int pin)
{
	return 0;
}

#else

int test_lvgl()
{
	return 0;
}

#endif
