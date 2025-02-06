#include "boot.h"

#ifdef CONFIG_BOOTLOADER_MCUBOOT

#include <zephyr/dfu/mcuboot.h>

int confirm_mcuboot_img()
{
	if (!boot_is_img_confirmed()) {
		return boot_write_img_confirmed();
	}
	return 0;
}

#else

int confirm_mcuboot_img()
{
	return 0;
}

#endif
