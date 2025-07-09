#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include <zephyr/kernel.h>

K_MSGQ_DEFINE(display_msgq, sizeof(unsigned int), 4, sizeof(uint32_t));

int test_lvgl();

void app_display_loop();

#endif
