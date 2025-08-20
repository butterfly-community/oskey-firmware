#ifdef CONFIG_DISPLAY

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include "wrapper.h"
#include "lvgl.h"

#ifdef CONFIG_LV_Z_DEMO_BENCHMARK
#include <lv_demos.h>
#endif

const struct device *display_dev;

int app_init_display()
{
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		return 0;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	struct gpio_dt_spec lcd_backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);

	if (!gpio_is_ready_dt(&lcd_backlight)) {
		return 0;
	}

	gpio_pin_configure_dt(&lcd_backlight, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&lcd_backlight, 1);
#endif

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);

#ifdef CONFIG_LV_Z_DEMO_BENCHMARK
	lv_demo_benchmark();
	return 0;
#endif

	return 0;
}

int app_display_hello()
{
	char str[12] = "Hello World\0";
	lv_obj_t *pin_label;

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);

	pin_label = lv_label_create(lv_screen_active());
	lv_label_set_text(pin_label, str);
	lv_obj_align(pin_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_color(pin_label, lv_color_white(), LV_PART_MAIN);

	return 0;
}

void app_display_loop()
{
	lv_timer_handler();
	display_blanking_off(display_dev);

	app_display_hello();

	while (1) {
		uint32_t sleep_ms = lv_timer_handler();
		k_msleep(MIN(sleep_ms, INT32_MAX));
	}
}

#else

void app_display_loop()
{
	return;
}

int app_init_display()
{
	return 0;
}

#endif
