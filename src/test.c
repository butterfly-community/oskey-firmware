#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_test);

static void confirmation_required(const struct zbus_channel *channel)
{
	const struct app_confirmation_state *state = zbus_chan_const_msg(channel);

	if (state->phase != APP_CONFIRMATION_REQUIRED) {
		return;
	}

	int ret = app_core_submit_confirmation(state->id, ConfirmationChoice_Approve, K_NO_WAIT);
	if (ret < 0) {
		LOG_ERR("Failed to approve confirmation %u: %d", state->id, ret);
	}
}

ZBUS_LISTENER_DEFINE(test_confirmation_listener, confirmation_required);
ZBUS_CHAN_ADD_OBS(app_confirmation_state_chan, test_confirmation_listener, 0);
