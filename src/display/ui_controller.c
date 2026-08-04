#include "ui.h"

#ifndef CONFIG_OSKEY_LVGL_BENCHMARK

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <lvgl_zephyr.h>

enum ui_pending_event {
	UI_EVENT_STATUS = BIT(0),
	UI_EVENT_NETWORK = BIT(1),
	UI_EVENT_CONFIRMATION = BIT(2),
	UI_EVENT_LOCAL_RESULT = BIT(3),
};

struct ui_pending_network {
	uint32_t kinds;
	struct app_wifi_state wifi;
	struct app_wifi_config config;
	struct app_wifi_scan scan;
	struct app_public_ip public_ip;
};

static lv_timer_t *startup_timer;
static atomic_t ui_initialized;
static atomic_t pending_events;
static struct ui_pending_network pending_network;
K_SEM_DEFINE(event_sem, 0, 1);
K_MUTEX_DEFINE(network_pending_lock);

static void read_device_status(struct ui_status *status)
{
	(void)zbus_chan_read(&app_bluetooth_state_chan, &status->bluetooth, K_FOREVER);
	(void)zbus_chan_read(&app_usb_state_chan, &status->usb, K_FOREVER);
	(void)zbus_chan_read(&app_storage_state_chan, &status->storage, K_FOREVER);
	(void)zbus_chan_read(&app_wallet_state_chan, &status->wallet, K_FOREVER);
}

static void cancel_startup(void)
{
	if (startup_timer != NULL) {
		lv_timer_delete(startup_timer);
		startup_timer = NULL;
	}
}

static const char *error_text(AppError error, uint32_t value, char *buffer, size_t size)
{
	switch (error) {
	case AppError_Busy:
		return "Device busy";
	case AppError_Rejected:
		return "Request rejected";
	case AppError_Locked:
		return "Wallet is locked";
	case AppError_NoPendingAction:
		return "No action pending";
	case AppError_DisplayRequired:
		return "Use the device display";
	case AppError_ExternalRequestRequired:
		return "External request required";
	case AppError_TrustedActionRequired:
		return "Trusted confirmation required";
	case AppError_InvalidAction:
		return "Invalid action";
	case AppError_UnlockFailed:
		snprintk(buffer, size, "Unlock failed (%u/10)", value);
		return buffer;
	case AppError_Unspecified:
	case AppError_Failed:
	default:
		return "Operation failed";
	}
}

static bool active_status_page(void)
{
	switch (ui.page) {
	case UI_PAGE_WIFI:
	case UI_PAGE_BLUETOOTH:
	case UI_PAGE_USB:
		return true;
	default:
		return false;
	}
}

static void apply_status(const struct ui_status *next)
{
	struct ui_status previous = ui.status;

	ui.status = *next;
	ui_status_update(next);
	if (active_status_page()) {
		ui_refresh();
	}
	if (previous.storage != APP_STORAGE_ERROR && next->storage == APP_STORAGE_ERROR) {
		ui_open(UI_PAGE_STORAGE_ERROR);
	} else if (previous.wallet != WalletState_Locked && next->wallet == WalletState_Locked) {
		ui_open(UI_PAGE_LOCKED);
	}
}

static void apply_wifi_scan(const struct app_wifi_scan *scan)
{
	ui.wifi_scan = *scan;
	if (ui.page == UI_PAGE_WIFI) {
		ui_refresh();
	}
}

static void apply_wifi_config(const struct app_wifi_config *config)
{
	ui.wifi_config = *config;
	if (ui.page == UI_PAGE_WIFI) {
		ui_refresh();
	}
}

static void process_network_events(void)
{
	struct ui_pending_network events;

	k_mutex_lock(&network_pending_lock, K_FOREVER);
	events = pending_network;
	pending_network.kinds = 0;
	k_mutex_unlock(&network_pending_lock);

	struct ui_status next_status = ui.status;
	bool status_changed = false;
	if ((events.kinds & BIT(APP_NETWORK_EVENT_WIFI_STATE)) != 0) {
		next_status.wifi = events.wifi;
		status_changed = true;
	}
	if ((events.kinds & BIT(APP_NETWORK_EVENT_PUBLIC_IP)) != 0) {
		next_status.public_ip = events.public_ip;
		status_changed = true;
	}
	if (status_changed) {
		apply_status(&next_status);
	}
	if ((events.kinds & BIT(APP_NETWORK_EVENT_WIFI_CONFIG)) != 0) {
		apply_wifi_config(&events.config);
	}
	if ((events.kinds & BIT(APP_NETWORK_EVENT_WIFI_SCAN)) != 0) {
		apply_wifi_scan(&events.scan);
	}
}

static void apply_confirmation(const struct app_confirmation_state *state)
{
	if (state->phase == APP_CONFIRMATION_REQUIRED) {
		cancel_startup();
		if (ui.page == UI_PAGE_SPLASH) {
			ui_show_startup();
		}
		ui_set_busy(false);
		ui_show_confirmation(state->id);
		return;
	}

	if (ui.confirmation_id != 0) {
		ui_dismiss_confirmation();
	}
}

static void handle_local_result(struct app_local_result *result)
{
	cancel_startup();
	ui_set_busy(false);

	switch (result->action) {
	case LocalAction_Ready:
		ui_open(UI_PAGE_HOME);
		break;
	case LocalAction_Mnemonic: {
		ui_wipe(ui.mnemonic, sizeof(ui.mnemonic));
		size_t len = MIN(app_payload_length(result->payload), sizeof(ui.mnemonic) - 1);
		if (app_payload_read(result->payload, 0, ui.mnemonic, len) != len) {
			len = 0;
		}
		ui.mnemonic[len] = '\0';
		ui_wipe(ui.entropy, sizeof(ui.entropy));
		ui.entropy_bits = 0;
		ui.custom_entropy = false;
		ui_push(UI_PAGE_MNEMONIC);
		break;
	}
	case LocalAction_Error: {
		char buffer[32];
		const char *text = error_text(result->error, result->value, buffer, sizeof(buffer));
		if (result->error == AppError_UnlockFailed && ui.page == UI_PAGE_LOCKED) {
			ui_input_error(text);
		} else {
			ui_error(text);
		}
		break;
	}
	}
}

static void process_local_results(void)
{
	struct app_local_result result;
	while (app_local_result_get(&result, K_NO_WAIT) == 0) {
		handle_local_result(&result);
		app_payload_release(result.payload);
	}
}

static bool network_event_store(const struct app_network_event *event)
{
	if (event->kind <= APP_NETWORK_EVENT_NONE || event->kind > APP_NETWORK_EVENT_PUBLIC_IP) {
		return false;
	}

	k_mutex_lock(&network_pending_lock, K_FOREVER);
	switch (event->kind) {
	case APP_NETWORK_EVENT_WIFI_STATE:
		pending_network.wifi = event->data.wifi;
		break;
	case APP_NETWORK_EVENT_WIFI_CONFIG:
		pending_network.config = event->data.config;
		break;
	case APP_NETWORK_EVENT_WIFI_SCAN:
		pending_network.scan = event->data.scan;
		break;
	case APP_NETWORK_EVENT_PUBLIC_IP:
		pending_network.public_ip = event->data.public_ip;
		break;
	case APP_NETWORK_EVENT_NONE:
	default:
		break;
	}
	pending_network.kinds |= BIT(event->kind);
	k_mutex_unlock(&network_pending_lock);
	return true;
}

static void ui_bus_changed(const struct zbus_channel *channel)
{
	atomic_val_t events = 0;

	if (channel == &app_network_event_chan) {
		const struct app_network_event *network = zbus_chan_const_msg(channel);

		if (network_event_store(network)) {
			events = UI_EVENT_NETWORK;
		}
	} else if (channel == &app_bluetooth_state_chan || channel == &app_usb_state_chan ||
		   channel == &app_storage_state_chan || channel == &app_wallet_state_chan) {
		events = UI_EVENT_STATUS;
	} else if (channel == &app_local_result_event_chan) {
		events = UI_EVENT_LOCAL_RESULT;
	} else if (channel == &app_confirmation_state_chan) {
		events = UI_EVENT_CONFIRMATION;
	}

	if (events != 0) {
		atomic_or(&pending_events, events);
		k_sem_give(&event_sem);
	}
}

ZBUS_LISTENER_DEFINE(ui_bus_listener, ui_bus_changed);
ZBUS_CHAN_ADD_OBS(app_network_event_chan, ui_bus_listener, 0);
ZBUS_CHAN_ADD_OBS(app_bluetooth_state_chan, ui_bus_listener, 0);
ZBUS_CHAN_ADD_OBS(app_usb_state_chan, ui_bus_listener, 0);
ZBUS_CHAN_ADD_OBS(app_storage_state_chan, ui_bus_listener, 0);
ZBUS_CHAN_ADD_OBS(app_wallet_state_chan, ui_bus_listener, 0);
ZBUS_CHAN_ADD_OBS(app_local_result_event_chan, ui_bus_listener, 0);
ZBUS_CHAN_ADD_OBS(app_confirmation_state_chan, ui_bus_listener, 0);

static void ui_event_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	while (true) {
		k_sem_take(&event_sem, K_FOREVER);
		if (atomic_get(&ui_initialized) == 0) {
			continue;
		}

		lvgl_lock();
		atomic_val_t events;
		while ((events = atomic_set(&pending_events, 0)) != 0) {
			if ((events & UI_EVENT_STATUS) != 0) {
				struct ui_status next_status = ui.status;

				read_device_status(&next_status);
				apply_status(&next_status);
			}
			if ((events & UI_EVENT_NETWORK) != 0) {
				process_network_events();
			}
			if ((events & UI_EVENT_LOCAL_RESULT) != 0) {
				process_local_results();
			}
			if ((events & UI_EVENT_CONFIRMATION) != 0) {
				struct app_confirmation_state next_confirmation;

				(void)zbus_chan_read(&app_confirmation_state_chan,
						     &next_confirmation, K_FOREVER);
				apply_confirmation(&next_confirmation);
			}
		}
		lvgl_unlock();
	}
}

K_THREAD_DEFINE(ui_event_thread_id, CONFIG_OSKEY_DISPLAY_EVENT_STACK_SIZE, ui_event_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(5), 0, 0);

static void startup_expired(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	startup_timer = NULL;
	ui_show_startup();
}

void ui_controller_init(const uint8_t features[APP_FEATURE_COUNT])
{
	struct ui_status initial_status = {
		.wifi =
			{
				.ap = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_AP_OFF
								    : APP_WIFI_AP_DISABLED,
				.sta = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_STA_OFF
								     : APP_WIFI_STA_DISABLED,
			},
	};
	struct app_wifi_config initial_config = {0};
	struct app_wifi_scan initial_scan = {
		.state =
			IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_SCAN_IDLE : APP_WIFI_SCAN_DISABLED,
	};
	struct app_confirmation_state initial_confirmation;

	read_device_status(&initial_status);
	(void)zbus_chan_read(&app_confirmation_state_chan, &initial_confirmation, K_FOREVER);

	ui_init(features, &initial_status, &initial_config, &initial_scan);
	ui_status_init(&initial_status);
	ui_open(UI_PAGE_SPLASH);
	startup_timer = lv_timer_create(startup_expired, 1200, NULL);
	if (startup_timer == NULL) {
		ui_show_startup();
	} else {
		lv_timer_set_repeat_count(startup_timer, 1);
	}
	apply_confirmation(&initial_confirmation);
	atomic_set(&ui_initialized, 1);
	k_sem_give(&event_sem);
}

#endif
