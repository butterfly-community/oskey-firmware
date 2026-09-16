/* SPDX-License-Identifier: MPL-2.0 */

#ifndef OSKEY_BUS_H
#define OSKEY_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_OSKEY_RUST)
#include <zephyr/net_buf.h>
#endif
#if defined(CONFIG_OSKEY_MESSAGE_BUS)
#include <zephyr/zbus/zbus.h>
#endif

#include "bindings.h"

typedef struct net_buf app_payload;

#define APP_WIFI_SSID_MAX_LEN      32
#define APP_WIFI_PASSWORD_MAX_LEN  63
#define APP_IPV4_ADDR_MAX_LEN      15
#define APP_PUBLIC_IP_MAX_LEN      45
#define APP_WIFI_SCAN_MAX_NETWORKS 12

enum app_wifi_ap_state {
	APP_WIFI_AP_DISABLED,
	APP_WIFI_AP_OFF,
	APP_WIFI_AP_STARTING,
	APP_WIFI_AP_ACTIVE,
};

enum app_wifi_sta_state {
	APP_WIFI_STA_DISABLED,
	APP_WIFI_STA_OFF,
	APP_WIFI_STA_DISCONNECTED,
	APP_WIFI_STA_CONNECTING,
	APP_WIFI_STA_CONNECTED,
};

struct app_dhcp_info {
	char address[APP_IPV4_ADDR_MAX_LEN + 1];
	char netmask[APP_IPV4_ADDR_MAX_LEN + 1];
	char gateway[APP_IPV4_ADDR_MAX_LEN + 1];
	uint32_t lease_seconds;
};

struct app_wifi_state {
	enum app_wifi_ap_state ap;
	enum app_wifi_sta_state sta;
	bool ap_client_connected;
	char connected_ssid[APP_WIFI_SSID_MAX_LEN + 1];
	struct app_dhcp_info dhcp;
};

struct app_wifi_config {
	bool sta_enabled;
	bool ap_enabled;
	bool restart_required;
	char saved_ssid[APP_WIFI_SSID_MAX_LEN + 1];
};

struct app_public_ip {
	char address[APP_PUBLIC_IP_MAX_LEN + 1];
};

enum app_wifi_security {
	APP_WIFI_SECURITY_OPEN,
	APP_WIFI_SECURITY_PERSONAL,
	APP_WIFI_SECURITY_SAE,
	APP_WIFI_SECURITY_UNSUPPORTED,
};

enum app_wifi_command_kind {
	APP_WIFI_COMMAND_NONE,
	APP_WIFI_COMMAND_SET_STA,
	APP_WIFI_COMMAND_SET_AP,
	APP_WIFI_COMMAND_SCAN,
	APP_WIFI_COMMAND_SAVE_NETWORK,
	APP_WIFI_COMMAND_FORGET_NETWORK,
};

struct app_wifi_command {
	enum app_wifi_command_kind kind;
	enum app_wifi_security security;
	bool enabled;
	uint8_t ssid_len;
	uint8_t password_len;
	char ssid[APP_WIFI_SSID_MAX_LEN + 1];
	char password[APP_WIFI_PASSWORD_MAX_LEN + 1];
};

enum app_wifi_scan_state {
	APP_WIFI_SCAN_DISABLED,
	APP_WIFI_SCAN_IDLE,
	APP_WIFI_SCAN_SCANNING,
	APP_WIFI_SCAN_READY,
	APP_WIFI_SCAN_ERROR,
};

struct app_wifi_network {
	char ssid[APP_WIFI_SSID_MAX_LEN + 1];
	enum app_wifi_security security;
	int8_t rssi;
};

struct app_wifi_scan {
	enum app_wifi_scan_state state;
	uint32_t generation;
	uint8_t count;
	struct app_wifi_network networks[APP_WIFI_SCAN_MAX_NETWORKS];
};

enum app_network_event_kind {
	APP_NETWORK_EVENT_NONE,
	APP_NETWORK_EVENT_WIFI_STATE,
	APP_NETWORK_EVENT_WIFI_CONFIG,
	APP_NETWORK_EVENT_WIFI_SCAN,
	APP_NETWORK_EVENT_PUBLIC_IP,
};

struct app_network_event {
	enum app_network_event_kind kind;
	union {
		struct app_wifi_state wifi;
		struct app_wifi_config config;
		struct app_wifi_scan scan;
		struct app_public_ip public_ip;
	} data;
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

enum app_camera_state {
	APP_CAMERA_DISABLED,
	APP_CAMERA_INITIALIZING,
	APP_CAMERA_READY,
	APP_CAMERA_STARTING,
	APP_CAMERA_ACTIVE,
	APP_CAMERA_ERROR,
};

enum app_audio_state {
	APP_AUDIO_DISABLED,
	APP_AUDIO_IDLE,
	APP_AUDIO_PLAYING,
};

enum app_audio_command_kind {
	APP_AUDIO_COMMAND_NONE,
	APP_AUDIO_COMMAND_BEEP,
	APP_AUDIO_COMMAND_SET_VOLUME,
	APP_AUDIO_COMMAND_SET_MICROPHONE,
};

struct app_audio_command {
	enum app_audio_command_kind kind;
	uint8_t volume;
	bool enabled;
};

struct app_audio_status {
	enum app_audio_state state;
	uint8_t volume;
	bool microphone_enabled;
};

enum app_imu_state {
	APP_IMU_DISABLED,
	APP_IMU_INITIALIZING,
	APP_IMU_IDLE,
	APP_IMU_READY,
	APP_IMU_ERROR,
};

struct app_imu_sample {
	bool valid;
	float rotation[9];
};

enum app_imu_command_kind {
	APP_IMU_COMMAND_NONE,
	APP_IMU_COMMAND_START,
	APP_IMU_COMMAND_STOP,
};

enum app_imu_client {
	APP_IMU_CLIENT_UI,
	APP_IMU_CLIENT_ENTROPY,
	APP_IMU_CLIENT_COUNT,
};

struct app_imu_command {
	enum app_imu_command_kind kind;
	enum app_imu_client client;
};

enum app_notification_kind {
	APP_NOTIFICATION_NONE,
	APP_NOTIFICATION_SUCCESS,
};

struct app_notification {
	enum app_notification_kind kind;
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

#if defined(CONFIG_OSKEY_RUST) && defined(CONFIG_OSKEY_DISPLAY)
ZBUS_CHAN_DECLARE(app_local_result_event_chan);
#endif
#if defined(CONFIG_OSKEY_WIFI)
ZBUS_CHAN_DECLARE(app_wifi_command_chan, app_network_event_chan);
#endif
#if defined(CONFIG_OSKEY_BLUETOOTH)
ZBUS_CHAN_DECLARE(app_bluetooth_state_chan);
#endif
#if defined(CONFIG_OSKEY_USB)
ZBUS_CHAN_DECLARE(app_usb_state_chan);
#endif
#if defined(CONFIG_OSKEY_STORAGE)
ZBUS_CHAN_DECLARE(app_storage_state_chan);
#endif
#if defined(CONFIG_OSKEY_CAMERA)
ZBUS_CHAN_DECLARE(app_camera_state_chan);
#endif
#if defined(CONFIG_OSKEY_AUDIO)
ZBUS_CHAN_DECLARE(app_audio_state_chan, app_audio_command_chan);
#endif
#if defined(CONFIG_OSKEY_IMU)
ZBUS_CHAN_DECLARE(app_imu_state_chan, app_imu_sample_chan, app_imu_command_chan);
#endif
#if defined(CONFIG_OSKEY_AUDIO) && defined(CONFIG_OSKEY_QR_SCANNER)
ZBUS_CHAN_DECLARE(app_notification_event_chan);
#endif

#if defined(CONFIG_OSKEY_MESSAGE_BUS)
int app_core_submit_protocol(struct TransportRoute route, const void *data, size_t len,
			     k_timeout_t timeout);
#endif

#if defined(CONFIG_OSKEY_RUST)
ZBUS_CHAN_DECLARE(app_wallet_state_chan, app_confirmation_state_chan);

size_t app_payload_length(const app_payload *payload);
size_t app_payload_read(const app_payload *payload, size_t offset, void *data, size_t len);
size_t app_payload_slices(const app_payload *payload, struct AppSlice *slices, size_t capacity);
void app_payload_release(app_payload *payload);

#if defined(CONFIG_OSKEY_DISPLAY)
int app_core_submit_local(enum LocalRequestKind kind, uint32_t value, const void *data, size_t len,
			  const void *auxiliary, size_t auxiliary_len, k_timeout_t timeout);
#endif
#if defined(CONFIG_OSKEY_FIDO2)
int app_core_submit_fido(enum FidoRequestKind kind, uint32_t request_id, uint32_t value,
			 const void *data, size_t len, const void *auxiliary, size_t auxiliary_len,
			 k_timeout_t timeout);
#endif
int app_core_submit_confirmation(uint32_t id, enum ConfirmationChoice choice, k_timeout_t timeout);
int app_core_command_get(struct app_core_command *command, k_timeout_t timeout);
void app_bus_core_ready(void);

#if defined(CONFIG_OSKEY_DISPLAY)
int app_local_result_submit(enum LocalAction action, AppError error, uint32_t value,
			    const void *data, size_t len, k_timeout_t timeout);
int app_local_result_get(struct app_local_result *result, k_timeout_t timeout);
#endif

#if defined(CONFIG_OSKEY_FIDO2)
int app_fido_result_submit(uint32_t request_id, enum FidoStatus status, const void *credential_id,
			   size_t credential_id_len, const void *data, size_t len,
			   k_timeout_t timeout);
int app_fido_result_get(struct app_fido_result *result, k_timeout_t timeout);
#endif
#endif /* CONFIG_OSKEY_RUST */

#endif
