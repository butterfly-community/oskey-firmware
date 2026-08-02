#ifndef OSKEY_BUS_H
#define OSKEY_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/zbus/zbus.h>

#include "bindings.h"

typedef struct net_buf app_payload;

enum app_wifi_ap_state {
	APP_WIFI_AP_DISABLED,
	APP_WIFI_AP_OFF,
	APP_WIFI_AP_STARTING,
	APP_WIFI_AP_ACTIVE,
	APP_WIFI_AP_STOPPING,
};

enum app_wifi_sta_state {
	APP_WIFI_STA_DISABLED,
	APP_WIFI_STA_DISCONNECTED,
	APP_WIFI_STA_CONNECTING_STORED,
	APP_WIFI_STA_CONNECTING_NEW,
	APP_WIFI_STA_CONNECTED,
	APP_WIFI_STA_DISCONNECTING,
};

struct app_wifi_state {
	enum app_wifi_ap_state ap;
	enum app_wifi_sta_state sta;
};

enum app_bluetooth_state {
	APP_BLUETOOTH_DISABLED,
	APP_BLUETOOTH_IDLE,
	APP_BLUETOOTH_ADVERTISING,
	APP_BLUETOOTH_CONNECTED,
};

enum app_usb_state {
	APP_USB_DISABLED,
	APP_USB_DISCONNECTED,
	APP_USB_ATTACHED,
	APP_USB_CONFIGURED,
	APP_USB_SUSPENDED,
};

enum app_storage_state {
	APP_STORAGE_DISABLED,
	APP_STORAGE_INITIALIZING,
	APP_STORAGE_READY,
	APP_STORAGE_ERROR,
};

enum app_confirmation_phase {
	APP_CONFIRMATION_IDLE,
	APP_CONFIRMATION_REQUIRED,
	APP_CONFIRMATION_COMPLETED,
};

struct app_confirmation_state {
	uint32_t id;
	enum app_confirmation_phase phase;
	enum ConfirmationOutcome outcome;
};

struct app_core_command {
	enum AppCoreCommandKind kind;
	struct TransportRoute route;
	enum LocalRequestKind local_kind;
	enum FidoRequestKind fido_kind;
	enum ConfirmationChoice choice;
	uint32_t request_id;
	uint32_t value;
	size_t first_len;
	app_payload *payload;
};

struct app_transport_result {
	app_payload *payload;
	struct TransportRoute route;
};

struct app_local_result {
	app_payload *payload;
	enum LocalAction action;
	AppError error;
	uint32_t value;
};

struct app_fido_result {
	app_payload *payload;
	uint32_t request_id;
	size_t credential_id_len;
	enum FidoStatus status;
};

ZBUS_CHAN_DECLARE(app_wifi_state_chan, app_bluetooth_state_chan, app_usb_state_chan,
		  app_storage_state_chan, app_wallet_state_chan, app_confirmation_state_chan);

size_t app_payload_length(const app_payload *payload);
size_t app_payload_read(const app_payload *payload, size_t offset, void *data, size_t len);
size_t app_payload_slices(const app_payload *payload, struct AppSlice *slices, size_t capacity);
void app_payload_release(app_payload *payload);

int app_core_submit_protocol(struct TransportRoute route, const void *data, size_t len,
			     k_timeout_t timeout);
bool app_core_protocol_ready(void);
int app_core_submit_local(enum LocalRequestKind kind, uint32_t value, const void *data, size_t len,
			  const void *auxiliary, size_t auxiliary_len, k_timeout_t timeout);
int app_core_submit_fido(enum FidoRequestKind kind, uint32_t request_id, uint32_t value,
			 const void *data, size_t len, const void *auxiliary, size_t auxiliary_len,
			 k_timeout_t timeout);
int app_core_submit_confirmation(uint32_t id, enum ConfirmationChoice choice, k_timeout_t timeout);
int app_core_command_get(struct app_core_command *command, k_timeout_t timeout);
void app_bus_core_ready(void);

int app_transport_result_submit(struct TransportRoute route, const void *data, size_t len,
				k_timeout_t timeout);
int app_transport_result_get(struct app_transport_result *result, k_timeout_t timeout);

int app_local_result_submit(enum LocalAction action, AppError error, uint32_t value,
			    const void *data, size_t len, k_timeout_t timeout);
int app_local_result_get(struct app_local_result *result, k_timeout_t timeout);

int app_fido_result_submit(uint32_t request_id, enum FidoStatus status, const void *credential_id,
			   size_t credential_id_len, const void *data, size_t len,
			   k_timeout_t timeout);
int app_fido_result_get(struct app_fido_result *result, k_timeout_t timeout);

int app_confirmation_required_publish(uint32_t id);
int app_confirmation_completed_publish(uint32_t id, enum ConfirmationOutcome outcome);

void app_wifi_state_publish(const struct app_wifi_state *state);
void app_bluetooth_state_publish(enum app_bluetooth_state state);
void app_usb_state_publish(enum app_usb_state state);
void app_storage_state_publish(enum app_storage_state state);
void app_wallet_state_publish(enum WalletState state);

#endif
