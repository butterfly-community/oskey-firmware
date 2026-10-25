/* SPDX-License-Identifier: MPL-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ui_keyboard.h"

static lv_obj_t *keyboard;
static lv_obj_t *textarea;
static lv_indev_t *pointer;
static lv_indev_state_t state;
static lv_point_t point;

static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data)
{
	ARG_UNUSED(indev);
	data->state = state;
	data->point = point;
}

static void prepare(void *fixture)
{
	ARG_UNUSED(fixture);
	textarea = lv_textarea_create(lv_screen_active());
	lv_textarea_set_text(textarea, "");
	lv_obj_set_size(textarea, 320, 40);
	keyboard = ui_keyboard_create(lv_screen_active());
	lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 0, 80);
	lv_obj_set_size(keyboard, 320, 160);
	lv_obj_set_style_pad_all(keyboard, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_row(keyboard, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_column(keyboard, 0, LV_PART_MAIN);
	lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
	lv_keyboard_set_textarea(keyboard, textarea);
	lv_obj_update_layout(lv_screen_active());
	pointer = lv_indev_create();
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(pointer, read_pointer);
	lv_indev_set_long_press_time(pointer, 30);
	lv_indev_set_long_press_repeat_time(pointer, 10);
	state = LV_INDEV_STATE_RELEASED;
}

static void cleanup(void *fixture)
{
	ARG_UNUSED(fixture);
	lv_indev_delete(pointer);
	lv_obj_delete(keyboard);
	lv_obj_delete(textarea);
}

static void tap(int32_t x, int32_t y)
{
	lv_obj_update_layout(lv_screen_active());
	point = (lv_point_t){x, y};
	state = LV_INDEV_STATE_PRESSED;
	lv_indev_read(pointer);
	state = LV_INDEV_STATE_RELEASED;
	lv_indev_read(pointer);
}

static void hold(int32_t x, int32_t y, const char *expected)
{
	lv_obj_update_layout(lv_screen_active());
	point = (lv_point_t){x, y};
	state = LV_INDEV_STATE_PRESSED;
	lv_indev_read(pointer);
	zassert_str_equal(lv_textarea_get_text(textarea), expected, "Initial input: %s",
			 lv_textarea_get_text(textarea));
	/* A retained press must not keep adding characters to the textarea. */
	for (int i = 0; i < 20; i++) {
		k_sleep(K_MSEC(20));
		lv_indev_read(pointer);
	}
	zassert_str_equal(lv_textarea_get_text(textarea), expected);
	state = LV_INDEV_STATE_RELEASED;
	lv_indev_read(pointer);
	zassert_str_equal(lv_textarea_get_text(textarea), expected);
}

ZTEST(keyboard, test_held_digit_is_entered_once)
{
	lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
	hold(10, 90, "1");
}

ZTEST(keyboard, test_layout_switches_keep_single_input)
{
	tap(10, 130); /* ABC */
	zassert_equal(lv_keyboard_get_mode(keyboard), LV_KEYBOARD_MODE_TEXT_UPPER);
	hold(42, 90, "Q");
	tap(10, 130); /* abc */
	zassert_equal(lv_keyboard_get_mode(keyboard), LV_KEYBOARD_MODE_TEXT_LOWER);
	hold(42, 90, "Qq");
	tap(10, 90); /* 1# */
	zassert_equal(lv_keyboard_get_mode(keyboard), LV_KEYBOARD_MODE_SPECIAL);
	hold(10, 90, "Qq1");
}

ZTEST(keyboard, test_separate_taps_can_repeat_the_digit)
{
	lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
	lv_obj_update_layout(lv_screen_active());
	for (int i = 0; i < 3; i++) {
		tap(10, 90);
	}
	zassert_str_equal(lv_textarea_get_text(textarea), "111");
}

ZTEST(keyboard, test_backspace_is_single_action)
{
	lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
	lv_textarea_set_text(textarea, "1234");
	lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
	hold(250, 180, "123");
}

ZTEST_SUITE(keyboard, NULL, NULL, prepare, cleanup, NULL);
