#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include "bus.h"

#define SW0_NODE DT_ALIAS(sw0)

#if defined(CONFIG_GPIO) && DT_HAS_ALIAS(sw0) && DT_NODE_HAS_STATUS(SW0_NODE, okay)

LOG_MODULE_REGISTER(app_gpio);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;
static bool button_ready;
static atomic_t active_confirmation;
static atomic_t pressed_confirmation;
static struct k_work confirmation_work;

static void confirmation_changed(const struct zbus_channel *channel)
{
	const struct app_confirmation_state *state = zbus_chan_const_msg(channel);

	if (state->phase == APP_CONFIRMATION_REQUIRED) {
		atomic_set(&active_confirmation, state->id);
	} else if (state->phase == APP_CONFIRMATION_IDLE) {
		atomic_set(&active_confirmation, 0);
	} else {
		atomic_cas(&active_confirmation, state->id, 0);
	}
}

ZBUS_LISTENER_DEFINE(button_confirmation_listener, confirmation_changed);
ZBUS_CHAN_ADD_OBS(app_confirmation_state_chan, button_confirmation_listener, 0);

static void submit_confirmation(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t id = (uint32_t)atomic_set(&pressed_confirmation, 0);
	int ret = app_core_submit_confirmation(id, ConfirmationChoice_Approve, K_MSEC(100));

	if (ret < 0) {
		LOG_WRN("Failed to queue button confirmation: %d", ret);
	}
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	uint32_t id = atomic_get(&active_confirmation);
	if (id != 0 && atomic_cas(&pressed_confirmation, 0, id)) {
		k_work_submit(&confirmation_work);
	}
}

bool user_button_exists(void)
{
	return button_ready;
}

int user_button_init(void)
{
	button_ready = false;
	k_work_init(&confirmation_work, submit_confirmation);

	if (!gpio_is_ready_dt(&button)) {
		return -1;
	}

	int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret) {
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_INACTIVE);
	if (ret) {
		return ret;
	}

	button_ready = true;
	return 0;
}

#else

bool user_button_exists(void)
{
	return false;
}

int user_button_init(void)
{
	return -ENOTSUP;
}

#endif
