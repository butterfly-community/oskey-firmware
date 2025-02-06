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
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		// LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);

	if (IS_ENABLED(CONFIG_LV_Z_DEMO_BENCHMARK)) {
		lv_demo_benchmark();
		return;
	}

	return 0;
}

int app_display_bt_pin(unsigned int pin)
{
	char str[11] = {0};
	lv_obj_t *pin_label;

	printk("PIN: %d\n", pin);

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);

	pin_label = lv_label_create(lv_screen_active());
	sprintf(str, "%d", pin);
	lv_label_set_text(pin_label, str);
	lv_obj_align(pin_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_color(pin_label, lv_color_white(), LV_PART_MAIN);

	return 0;
}

void app_display_loop()
{
	lv_timer_handler();
	display_blanking_off(display_dev);

	int64_t msg_check_sleep = 0;

	while (1) {
		uint32_t sleep_ms = lv_timer_handler();

		uint32_t real_sleep = sleep_ms >= UINT32_MAX ? 100 : sleep_ms;

		unsigned int pin;

		if (msg_check_sleep > 200 && k_msgq_get(&display_msgq, &pin, K_NO_WAIT) == 0 ) {
			printf("PIN: %d\n", pin);
			app_display_bt_pin(pin);
			msg_check_sleep = 0;
		}

		msg_check_sleep += real_sleep;

		k_msleep(real_sleep);
	}
}

#else

void app_display_loop()
{
	return;
}

int test_lvgl()
{
	return 0;
}

#endif
