#include "bus.h"

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(app_bus);

#define APP_BUS_QUEUE_DEPTH 4

static atomic_t core_ready;

NET_BUF_POOL_FIXED_DEFINE(app_command_payload_pool, CONFIG_OSKEY_BUS_PAYLOAD_COUNT,
			  CONFIG_OSKEY_BUS_PAYLOAD_SIZE, 0, NULL);
NET_BUF_POOL_FIXED_DEFINE(app_result_payload_pool, CONFIG_OSKEY_BUS_PAYLOAD_COUNT,
			  CONFIG_OSKEY_BUS_PAYLOAD_SIZE, 0, NULL);

K_MSGQ_DEFINE(app_core_command_queue, sizeof(struct app_core_command), APP_BUS_QUEUE_DEPTH,
	      __alignof__(struct app_core_command));
K_MSGQ_DEFINE(app_transport_result_queue, sizeof(struct app_transport_result), APP_BUS_QUEUE_DEPTH,
	      __alignof__(struct app_transport_result));
K_MSGQ_DEFINE(app_local_result_queue, sizeof(struct app_local_result), APP_BUS_QUEUE_DEPTH,
	      __alignof__(struct app_local_result));
K_MSGQ_DEFINE(app_fido_result_queue, sizeof(struct app_fido_result), APP_BUS_QUEUE_DEPTH,
	      __alignof__(struct app_fido_result));

ZBUS_CHAN_DEFINE(app_wifi_state_chan, struct app_wifi_state, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.ap = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_AP_OFF
								   : APP_WIFI_AP_DISABLED,
			       .sta = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_STA_DISCONNECTED
								    : APP_WIFI_STA_DISABLED));

ZBUS_CHAN_DEFINE(app_bluetooth_state_chan, enum app_bluetooth_state, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_BLUETOOTH) ? APP_BLUETOOTH_IDLE : APP_BLUETOOTH_DISABLED);

ZBUS_CHAN_DEFINE(app_usb_state_chan, enum app_usb_state, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_USB) ? APP_USB_DISCONNECTED : APP_USB_DISABLED);

ZBUS_CHAN_DEFINE(app_storage_state_chan, enum app_storage_state, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_STORAGE) ? APP_STORAGE_INITIALIZING
						  : APP_STORAGE_DISABLED);

ZBUS_CHAN_DEFINE(app_wallet_state_chan, enum WalletState, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_RUST) ? WalletState_Setup : WalletState_Disabled);

ZBUS_CHAN_DEFINE(app_confirmation_state_chan, struct app_confirmation_state, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.id = 0, .phase = APP_CONFIRMATION_IDLE,
			       .outcome = ConfirmationOutcome_Cancelled));

static struct net_buf *payload_alloc(k_timeout_t timeout, void *user_data)
{
	return net_buf_alloc(user_data, timeout);
}

static int payload_create(struct net_buf_pool *pool, const void *data, size_t len,
			  const void *auxiliary, size_t auxiliary_len, k_timeout_t timeout,
			  app_payload **result)
{
	if (pool == NULL || result == NULL || (len > 0 && data == NULL) ||
	    (auxiliary_len > 0 && auxiliary == NULL)) {
		return -EINVAL;
	}
	*result = NULL;

	if (len > SIZE_MAX - auxiliary_len ||
	    len + auxiliary_len >
		    (size_t)CONFIG_OSKEY_BUS_PAYLOAD_COUNT * CONFIG_OSKEY_BUS_PAYLOAD_SIZE) {
		return -EMSGSIZE;
	}
	if (len + auxiliary_len == 0) {
		return 0;
	}

	app_payload *payload = payload_alloc(timeout, pool);
	if (payload == NULL) {
		return -ENOMEM;
	}

	if ((len > 0 &&
	     net_buf_append_bytes(payload, len, data, timeout, payload_alloc, pool) != len) ||
	    (auxiliary_len > 0 && net_buf_append_bytes(payload, auxiliary_len, auxiliary, timeout,
						       payload_alloc, pool) != auxiliary_len)) {
		app_payload_release(payload);
		return -ENOMEM;
	}

	*result = payload;
	return 0;
}

size_t app_payload_length(const app_payload *payload)
{
	return payload == NULL ? 0 : net_buf_frags_len(payload);
}

size_t app_payload_read(const app_payload *payload, size_t offset, void *data, size_t len)
{
	if (len == 0) {
		return 0;
	}
	if (payload == NULL || data == NULL) {
		return 0;
	}
	return net_buf_linearize(data, len, payload, offset, len);
}

size_t app_payload_slices(const app_payload *payload, struct AppSlice *slices, size_t capacity)
{
	size_t count = 0;

	if (payload == NULL) {
		return 0;
	}
	if (slices == NULL || capacity == 0) {
		return 0;
	}

	for (const struct net_buf *fragment = payload; fragment != NULL;
	     fragment = fragment->frags) {
		if (count == capacity) {
			return 0;
		}
		slices[count++] = (struct AppSlice){
			.data = fragment->data,
			.len = fragment->len,
		};
	}
	return count;
}

void app_payload_release(app_payload *payload)
{
	if (payload == NULL) {
		return;
	}

	for (struct net_buf *fragment = payload; fragment != NULL; fragment = fragment->frags) {
		volatile uint8_t *data = fragment->data;

		for (size_t i = 0; i < fragment->len; i++) {
			data[i] = 0;
		}
	}
	net_buf_unref(payload);
}

static int queue_put(struct k_msgq *queue, const void *message, app_payload *payload,
		     k_timeout_t timeout)
{
	if (k_msgq_put(queue, message, timeout) == 0) {
		return 0;
	}

	app_payload_release(payload);
	return -EAGAIN;
}

static int queue_get(struct k_msgq *queue, void *message, k_timeout_t timeout)
{
	if (message == NULL) {
		return -EINVAL;
	}
	return k_msgq_get(queue, message, timeout) == 0 ? 0 : -EAGAIN;
}

static bool core_accepts_commands(void)
{
	return IS_ENABLED(CONFIG_OSKEY_RUST) && atomic_get(&core_ready) != 0;
}

void app_bus_core_ready(void)
{
	atomic_set(&core_ready, 1);
}

bool app_core_protocol_ready(void)
{
	return core_accepts_commands() && k_msgq_num_free_get(&app_core_command_queue) > 0;
}

int app_core_submit_protocol(struct TransportRoute route, const void *data, size_t len,
			     k_timeout_t timeout)
{
	if (!core_accepts_commands()) {
		return -ENOTSUP;
	}

	struct app_core_command command = {
		.kind = AppCoreCommandKind_Protocol,
		.route = route,
	};
	int ret = payload_create(&app_command_payload_pool, data, len, NULL, 0, timeout,
				 &command.payload);

	return ret < 0 ? ret
		       : queue_put(&app_core_command_queue, &command, command.payload, timeout);
}

int app_core_submit_local(enum LocalRequestKind kind, uint32_t value, const void *data, size_t len,
			  const void *auxiliary, size_t auxiliary_len, k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_DISPLAY) || !core_accepts_commands()) {
		return -ENOTSUP;
	}

	struct app_core_command command = {
		.kind = AppCoreCommandKind_Local,
		.value = value,
		.local_kind = kind,
		.first_len = len,
	};
	int ret = payload_create(&app_command_payload_pool, data, len, auxiliary, auxiliary_len,
				 timeout, &command.payload);

	return ret < 0 ? ret
		       : queue_put(&app_core_command_queue, &command, command.payload, timeout);
}

int app_core_submit_fido(enum FidoRequestKind kind, uint32_t request_id, uint32_t value,
			 const void *data, size_t len, const void *auxiliary, size_t auxiliary_len,
			 k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_FIDO2) || !core_accepts_commands()) {
		return -ENOTSUP;
	}

	struct app_core_command command = {
		.kind = AppCoreCommandKind_Fido,
		.request_id = request_id,
		.value = value,
		.fido_kind = kind,
		.first_len = len,
	};
	int ret = payload_create(&app_command_payload_pool, data, len, auxiliary, auxiliary_len,
				 timeout, &command.payload);

	return ret < 0 ? ret
		       : queue_put(&app_core_command_queue, &command, command.payload, timeout);
}

int app_core_submit_confirmation(uint32_t id, enum ConfirmationChoice choice, k_timeout_t timeout)
{
	if (!core_accepts_commands()) {
		return -ENOTSUP;
	}
	if (id == 0) {
		return -EINVAL;
	}

	struct app_core_command command = {
		.kind = AppCoreCommandKind_Confirm,
		.request_id = id,
		.choice = choice,
	};

	return queue_put(&app_core_command_queue, &command, NULL, timeout);
}

int app_core_command_get(struct app_core_command *command, k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_RUST)) {
		return -ENOTSUP;
	}
	return queue_get(&app_core_command_queue, command, timeout);
}

int app_transport_result_submit(struct TransportRoute route, const void *data, size_t len,
				k_timeout_t timeout)
{
	struct app_transport_result result = {.route = route};
	int ret = payload_create(&app_result_payload_pool, data, len, NULL, 0, timeout,
				 &result.payload);

	return ret < 0 ? ret
		       : queue_put(&app_transport_result_queue, &result, result.payload, timeout);
}

int app_transport_result_get(struct app_transport_result *result, k_timeout_t timeout)
{
	return queue_get(&app_transport_result_queue, result, timeout);
}

int app_local_result_submit(enum LocalAction action, AppError error, uint32_t value,
			    const void *data, size_t len, k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_DISPLAY)) {
		return -ENOTSUP;
	}

	struct app_local_result result = {
		.action = action,
		.error = error,
		.value = value,
	};
	int ret = payload_create(&app_result_payload_pool, data, len, NULL, 0, timeout,
				 &result.payload);

	return ret < 0 ? ret : queue_put(&app_local_result_queue, &result, result.payload, timeout);
}

int app_local_result_get(struct app_local_result *result, k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_DISPLAY)) {
		return -ENOTSUP;
	}
	return queue_get(&app_local_result_queue, result, timeout);
}

int app_fido_result_submit(uint32_t request_id, enum FidoStatus status, const void *credential_id,
			   size_t credential_id_len, const void *data, size_t len,
			   k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_FIDO2)) {
		return -ENOTSUP;
	}

	struct app_fido_result result = {
		.request_id = request_id,
		.credential_id_len = credential_id_len,
		.status = status,
	};
	int ret = payload_create(&app_result_payload_pool, credential_id, credential_id_len, data,
				 len, timeout, &result.payload);

	return ret < 0 ? ret : queue_put(&app_fido_result_queue, &result, result.payload, timeout);
}

int app_fido_result_get(struct app_fido_result *result, k_timeout_t timeout)
{
	if (!IS_ENABLED(CONFIG_OSKEY_FIDO2)) {
		return -ENOTSUP;
	}
	return queue_get(&app_fido_result_queue, result, timeout);
}

static int confirmation_publish(uint32_t id, enum app_confirmation_phase phase,
				enum ConfirmationOutcome outcome)
{
	struct app_confirmation_state state = {
		.id = id,
		.phase = phase,
		.outcome = outcome,
	};
	int ret = zbus_chan_pub(&app_confirmation_state_chan, &state, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish confirmation state: %d", ret);
	}
	return ret;
}

int app_confirmation_required_publish(uint32_t id)
{
	if (id == 0) {
		return -EINVAL;
	}
	return confirmation_publish(id, APP_CONFIRMATION_REQUIRED, ConfirmationOutcome_Cancelled);
}

int app_confirmation_completed_publish(uint32_t id, enum ConfirmationOutcome outcome)
{
	if (id == 0) {
		return -EINVAL;
	}
	return confirmation_publish(id, APP_CONFIRMATION_COMPLETED, outcome);
}

static void publish_state(const struct zbus_channel *channel, const void *state)
{
	if (state == NULL) {
		return;
	}

	int ret = zbus_chan_pub(channel, state, K_FOREVER);

	if (ret < 0) {
		LOG_ERR("Failed to publish application state: %d", ret);
	}
}

void app_wifi_state_publish(const struct app_wifi_state *state)
{
	publish_state(&app_wifi_state_chan, state);
}

void app_bluetooth_state_publish(enum app_bluetooth_state state)
{
	publish_state(&app_bluetooth_state_chan, &state);
}

void app_usb_state_publish(enum app_usb_state state)
{
	publish_state(&app_usb_state_chan, &state);
}

void app_storage_state_publish(enum app_storage_state state)
{
	publish_state(&app_storage_state_chan, &state);
}

void app_wallet_state_publish(enum WalletState state)
{
	publish_state(&app_wallet_state_chan, &state);
}
