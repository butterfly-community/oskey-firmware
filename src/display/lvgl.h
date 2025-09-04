#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include <zephyr/kernel.h>

int app_init_display();

void app_display_loop();

int app_init_display();

void app_display_init_show_select_length();

void app_display_import_mnemonic();

void app_display_tools();

#endif
