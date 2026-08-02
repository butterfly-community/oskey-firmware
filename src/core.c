#include "core.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_core);

#define APP_CORE_ROUTE_TIMEOUT K_MSEC(100)

K_THREAD_STACK_DEFINE(app_core_stack, CONFIG_OSKEY_CORE_STACK_SIZE);
static struct k_thread app_core_thread_data;
static struct AppCore *core;

K_MUTEX_DEFINE(confirmation_lock);
static struct AppConfirmation confirmation;

static bool confirmation_store(uint32_t id)
{
	k_mutex_lock(&confirmation_lock, K_FOREVER);
	bool stored = app_core_confirmation_get_rs(core, id, &confirmation);
	if (!stored) {
		memset(&confirmation, 0, sizeof(confirmation));
	}
	k_mutex_unlock(&confirmation_lock);
	return stored;
}

static void confirmation_clear(uint32_t id)
{
	k_mutex_lock(&confirmation_lock, K_FOREVER);
	if (confirmation.id == id) {
		memset(&confirmation, 0, sizeof(confirmation));
	}
	k_mutex_unlock(&confirmation_lock);
}

bool app_core_confirmation_get(uint32_t id, struct AppConfirmation *result)
{
	if (id == 0 || result == NULL) {
		return false;
	}

	k_mutex_lock(&confirmation_lock, K_FOREVER);
	bool found = confirmation.id == id;
	if (found) {
		*result = confirmation;
	}
	k_mutex_unlock(&confirmation_lock);
	return found;
}

static int route_effect(const struct AppCoreEffectView *effect)
{
	int ret;

	switch (effect->kind) {
	case AppCoreEffectKind_Transport:
		return app_transport_result_submit(effect->route, effect->data.data,
						   effect->data.len, APP_CORE_ROUTE_TIMEOUT);
	case AppCoreEffectKind_Local:
		return app_local_result_submit(effect->local_action, effect->error, effect->value,
					       effect->data.data, effect->data.len,
					       APP_CORE_ROUTE_TIMEOUT);
	case AppCoreEffectKind_Fido:
		return app_fido_result_submit(effect->request_id, effect->fido_status,
					      effect->data.data, effect->data.len,
					      effect->auxiliary.data, effect->auxiliary.len,
					      APP_CORE_ROUTE_TIMEOUT);
	case AppCoreEffectKind_ConfirmationRequired:
		if (!confirmation_store(effect->id)) {
			return -EMSGSIZE;
		}
		return app_confirmation_required_publish(effect->id);
	case AppCoreEffectKind_ConfirmationCompleted:
		ret = app_confirmation_completed_publish(effect->id, effect->outcome);
		if (ret == 0) {
			confirmation_clear(effect->id);
		}
		return ret;
	case AppCoreEffectKind_WalletState:
		app_wallet_state_publish(effect->wallet_state);
		return 0;
	default:
		return -EINVAL;
	}
}

static uint32_t drain_effects(void)
{
	struct AppCoreEffectView effect;
	uint32_t reject = 0;

	while (app_core_effect_next_rs(core, &effect)) {
		int ret = route_effect(&effect);

		if (ret < 0) {
			LOG_ERR("Failed to route core effect %d: %d", effect.kind, ret);
			if (effect.kind == AppCoreEffectKind_ConfirmationRequired) {
				confirmation_clear(effect.id);
				reject = effect.id;
			}
		}
	}
	return reject;
}

static void reject_unpublished_confirmation(uint32_t id)
{
	while (id != 0) {
		struct AppCoreCommandView command = {
			.kind = AppCoreCommandKind_Confirm,
			.choice = ConfirmationChoice_Reject,
			.request_id = id,
		};

		if (!app_core_execute_rs(core, &command)) {
			LOG_ERR("Failed to reject unpublished confirmation %u", id);
			return;
		}
		id = drain_effects();
	}
}

static void execute(const struct app_core_command *command)
{
	struct AppSlice slices[CONFIG_OSKEY_BUS_PAYLOAD_COUNT];
	size_t count = app_payload_slices(command->payload, slices, ARRAY_SIZE(slices));
	struct AppCoreCommandView view = {
		.kind = command->kind,
		.route = command->route,
		.local_kind = command->local_kind,
		.fido_kind = command->fido_kind,
		.choice = command->choice,
		.request_id = command->request_id,
		.value = command->value,
		.first_len = command->first_len,
		.fragments = count == 0 ? NULL : slices,
		.fragment_count = count,
	};

	if (command->payload != NULL && count == 0) {
		LOG_ERR("Invalid core command payload");
		app_payload_release(command->payload);
		return;
	}

	bool executed = app_core_execute_rs(core, &view);
	app_payload_release(command->payload);

	if (!executed) {
		LOG_ERR("Core rejected command %d", command->kind);
		int ret = 0;

		if (command->kind == AppCoreCommandKind_Local) {
			ret = app_local_result_submit(LocalAction_Error, AppError_Failed, 0, NULL,
						      0, K_NO_WAIT);
		} else if (command->kind == AppCoreCommandKind_Fido) {
			ret = app_fido_result_submit(command->request_id, FidoStatus_Failed, NULL,
						     0, NULL, 0, K_NO_WAIT);
		}
		if (ret < 0) {
			LOG_ERR("Failed to route rejected command: %d", ret);
		}
	} else {
		reject_unpublished_confirmation(drain_effects());
	}
}

static void app_core_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct app_core_command command;
		int ret = app_core_command_get(&command, K_FOREVER);

		if (ret < 0) {
			LOG_ERR("Failed to receive core command: %d", ret);
			continue;
		}
		execute(&command);
	}
}

int app_core_init(void)
{
	if (core != NULL) {
		return -EALREADY;
	}

	core = app_core_create_rs();
	app_wallet_state_publish(app_core_state_rs(core));
	k_thread_create(&app_core_thread_data, app_core_stack,
			K_THREAD_STACK_SIZEOF(app_core_stack), app_core_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	app_bus_core_ready();
	return 0;
}
