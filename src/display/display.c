/* SPDX-License-Identifier: MPL-2.0 */

#include "display.h"

#ifdef CONFIG_OSKEY_DISPLAY

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "app.h"
#include "ui.h"

#ifdef CONFIG_OSKEY_LVGL_BENCHMARK
#include <lv_demos.h>
#endif

static bool display_ready;

int app_init_display(void)
{
	const struct device *display_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int ret;
	display_ready = false;

	if (!device_is_ready(display_device)) {
		return -ENODEV;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);
	if (!gpio_is_ready_dt(&backlight)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
#endif

	ret = display_blanking_off(display_device);
	if (ret < 0 && ret != -ENOSYS) {
		return ret;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	ret = gpio_pin_set_dt(&backlight, 1);
	if (ret < 0) {
		(void)display_blanking_on(display_device);
		return ret;
	}
#endif
	display_ready = true;

	lvgl_lock();
#ifdef CONFIG_OSKEY_LVGL_BENCHMARK
	lv_demo_benchmark();
#else
	uint8_t features[APP_FEATURE_COUNT];
	if (!app_check_feature(features, sizeof(features))) {
		display_ready = false;
		lvgl_unlock();
#if DT_NODE_EXISTS(DT_ALIAS(backlight))
		(void)gpio_pin_set_dt(&backlight, 0);
#endif
		(void)display_blanking_on(display_device);
		return -EINVAL;
	}
	ui_controller_init(features);
#endif
	lvgl_unlock();

	return 0;
}

bool app_display_ready(void)
{
	return display_ready;
}

#else

int app_init_display(void)
{
	return 0;
}

bool app_display_ready(void)
{
	return false;
}

#endif
