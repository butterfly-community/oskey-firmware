#include "boot.h"

#ifdef CONFIG_OSKEY_MCUBOOT

#include <zephyr/dfu/mcuboot.h>

int confirm_mcuboot_img(void)
{
	if (!boot_is_img_confirmed()) {
		return boot_write_img_confirmed();
	}
	return 0;
}

#else

int confirm_mcuboot_img(void)
{
	return 0;
}

#endif
