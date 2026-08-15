/* SPDX-License-Identifier: MPL-2.0 */

#define _DEFAULT_SOURCE

#include "bus.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
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
K_MSGQ_DEFINE(app_local_result_queue, sizeof(struct app_local_result), APP_BUS_QUEUE_DEPTH,
	      __alignof__(struct app_local_result));
K_MSGQ_DEFINE(app_fido_result_queue, sizeof(struct app_fido_result), APP_BUS_QUEUE_DEPTH,
	      __alignof__(struct app_fido_result));

ZBUS_CHAN_DEFINE(app_local_result_event_chan, bool, NULL, NULL, ZBUS_OBSERVERS_EMPTY, false);

ZBUS_CHAN_DEFINE(app_wifi_command_chan, struct app_wifi_command, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.kind = APP_WIFI_COMMAND_NONE));

ZBUS_CHAN_DEFINE(app_network_event_chan, struct app_network_event, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.kind = APP_NETWORK_EVENT_NONE));

ZBUS_CHAN_DEFINE(app_bluetooth_state_chan, enum app_bluetooth_state, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_BLUETOOTH) ? APP_BLUETOOTH_IDLE : APP_BLUETOOTH_DISABLED);

ZBUS_CHAN_DEFINE(app_usb_state_chan, enum app_usb_state, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_USB) ? APP_USB_DISCONNECTED : APP_USB_DISABLED);

ZBUS_CHAN_DEFINE(app_storage_state_chan, enum app_storage_state, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_STORAGE) ? APP_STORAGE_INITIALIZING
						  : APP_STORAGE_DISABLED);

ZBUS_CHAN_DEFINE(app_camera_state_chan, enum app_camera_state, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_CAMERA) ? APP_CAMERA_INITIALIZING : APP_CAMERA_DISABLED);

ZBUS_CHAN_DEFINE(app_audio_state_chan, struct app_audio_status, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = IS_ENABLED(CONFIG_OSKEY_AUDIO) ? APP_AUDIO_IDLE
								       : APP_AUDIO_DISABLED,
			       .volume = 0, .microphone_enabled = false));

ZBUS_CHAN_DEFINE(app_audio_command_chan, struct app_audio_command, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.kind = APP_AUDIO_COMMAND_NONE, .volume = 0, .enabled = false));

ZBUS_CHAN_DEFINE(app_imu_state_chan, enum app_imu_state, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 IS_ENABLED(CONFIG_OSKEY_IMU) ? APP_IMU_INITIALIZING : APP_IMU_DISABLED);

ZBUS_CHAN_DEFINE(
	app_imu_sample_chan, struct app_imu_sample, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
	ZBUS_MSG_INIT(.valid = false,
		       .rotation = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}));

ZBUS_CHAN_DEFINE(app_imu_command_chan, struct app_imu_command, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.kind = APP_IMU_COMMAND_NONE));

ZBUS_CHAN_DEFINE(app_notification_event_chan, struct app_notification, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(.kind = APP_NOTIFICATION_NONE));

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
		explicit_bzero(fragment->data, fragment->len);
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

	if (ret < 0) {
		return ret;
	}
	ret = queue_put(&app_local_result_queue, &result, result.payload, timeout);
	if (ret < 0) {
		return ret;
	}

	bool event = true;
	ret = zbus_chan_pub(&app_local_result_event_chan, &event, K_FOREVER);
	if (ret < 0) {
		LOG_ERR("Failed to publish local result event: %d", ret);
	}
	return ret;
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
