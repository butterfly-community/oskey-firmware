#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include "wrapper.h"

LOG_MODULE_REGISTER(app_gpio);

#define SW0_NODE DT_ALIAS(sw0)

#if DT_HAS_ALIAS(sw0) && DT_NODE_HAS_STATUS(SW0_NODE, okay)

#define SW0_ENABLED true

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_work_submit(&app_sign_work);
	LOG_INF("User button pressed!");
}

bool user_button_exists(void)
{
	return true;
}

int user_button_init(void)
{
	if (!gpio_is_ready_dt(&button)) {
		return -1;
	}

	int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret) {
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	return 0;
}

#else

#define SW0_ENABLED false

bool user_button_exists(void)
{
	return false;
}

int user_button_init(void)
{
	return -1;
}
#endif
