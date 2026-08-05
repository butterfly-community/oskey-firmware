#ifndef BOOT_H
#define BOOT_H

#include <stdbool.h>

#ifdef CONFIG_OSKEY_MCUBOOT
int confirm_mcuboot_img(void);
bool app_update_mode_take(void);
#endif

#endif
