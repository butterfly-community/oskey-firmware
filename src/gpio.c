#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

const struct device *sw0_dev = NULL;

bool user_button_exists(void)
{
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
	sw0_dev = DEVICE_DT_GET(DT_ALIAS(sw0));
	if (!device_is_ready(sw0_dev)) {
		return false;
	}
	return true;
#else
  return false;
#endif
}
