#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

const struct device *sw0_dev = NULL;

bool user_button_exists()
{
#if DT_HAS_ALIAS(sw0) && DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
	return true;
#else
	return false;
#endif
}
