#include "display.h"

#ifdef CONFIG_OSKEY_DISPLAY

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "app.h"
#include "ui.h"

#ifdef CONFIG_OSKEY_LVGL_BENCHMARK
#include <lv_demos.h>
#endif

static bool display_ready;

#ifndef CONFIG_OSKEY_LVGL_BENCHMARK

#define DISPLAY_EVENT_PERIOD_MS 20

static lv_timer_t *startup_timer;
static bool ui_initialized;
static struct app_display_status status = {
	.wifi =
		{
			.ap = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_AP_OFF
							    : APP_WIFI_AP_DISABLED,
			.sta = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_WIFI_STA_DISCONNECTED
							     : APP_WIFI_STA_DISABLED,
		},
	.bluetooth =
		IS_ENABLED(CONFIG_OSKEY_BLUETOOTH) ? APP_BLUETOOTH_IDLE : APP_BLUETOOTH_DISABLED,
	.usb = IS_ENABLED(CONFIG_OSKEY_USB) ? APP_USB_DISCONNECTED : APP_USB_DISABLED,
	.storage =
		IS_ENABLED(CONFIG_OSKEY_STORAGE) ? APP_STORAGE_INITIALIZING : APP_STORAGE_DISABLED,
	.wallet = WalletState_Setup,
};

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

static void refresh_status(void)
{
	struct app_display_status previous = status;

	zbus_chan_read(&app_wifi_state_chan, &status.wifi, K_NO_WAIT);
	zbus_chan_read(&app_bluetooth_state_chan, &status.bluetooth, K_NO_WAIT);
	zbus_chan_read(&app_usb_state_chan, &status.usb, K_NO_WAIT);
	zbus_chan_read(&app_storage_state_chan, &status.storage, K_NO_WAIT);
	zbus_chan_read(&app_wallet_state_chan, &status.wallet, K_NO_WAIT);

	if (!ui_initialized || memcmp(&previous, &status, sizeof(status)) == 0) {
		return;
	}

	ui.status = status;
	ui_status_update(&status);
	if (previous.storage != APP_STORAGE_ERROR && status.storage == APP_STORAGE_ERROR) {
		ui_open(UI_PAGE_STORAGE_ERROR);
	} else if (previous.wallet != WalletState_Locked && status.wallet == WalletState_Locked) {
		ui_open(UI_PAGE_LOCKED);
	}
}

static void refresh_confirmation(void)
{
	struct app_confirmation_state state;

	if (zbus_chan_read(&app_confirmation_state_chan, &state, K_NO_WAIT) < 0) {
		return;
	}

	if (state.phase == APP_CONFIRMATION_REQUIRED) {
		if (ui.page == UI_PAGE_CONFIRMATION && ui.confirmation_id == state.id) {
			return;
		}

		bool new_confirmation = ui.confirmation_id != state.id;
		cancel_startup();
		if (ui.page == UI_PAGE_SPLASH) {
			ui_show_startup();
		}
		if (new_confirmation) {
			ui_set_busy(false);
		}
		ui_show_confirmation(state.id);
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

static void process_events(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	refresh_status();

	struct app_local_result result;
	while (app_local_result_get(&result, K_NO_WAIT) == 0) {
		handle_local_result(&result);
		app_payload_release(result.payload);
	}

	refresh_confirmation();
}

static void startup_expired(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	startup_timer = NULL;
	ui_show_startup();
}

#endif

int app_init_display(void)
{
	const struct device *display_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int ret;
	display_ready = false;

	if (!device_is_ready(display_device)) {
		return -ENODEV;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);
	if (!gpio_is_ready_dt(&backlight)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
#endif

	ret = display_blanking_off(display_device);
	if (ret < 0 && ret != -ENOSYS) {
		return ret;
	}

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	ret = gpio_pin_set_dt(&backlight, 1);
	if (ret < 0) {
		(void)display_blanking_on(display_device);
		return ret;
	}
#endif
	display_ready = true;

	lvgl_lock();
#ifdef CONFIG_OSKEY_LVGL_BENCHMARK
	lv_demo_benchmark();
#else
	uint8_t features[APP_FEATURE_COUNT];
	if (!app_check_feature(features, sizeof(features))) {
		display_ready = false;
		lvgl_unlock();
#if DT_NODE_EXISTS(DT_ALIAS(backlight))
		(void)gpio_pin_set_dt(&backlight, 0);
#endif
		(void)display_blanking_on(display_device);
		return -EINVAL;
	}
	refresh_status();
	if (lv_timer_create(process_events, DISPLAY_EVENT_PERIOD_MS, NULL) == NULL) {
		display_ready = false;
		lvgl_unlock();
#if DT_NODE_EXISTS(DT_ALIAS(backlight))
		(void)gpio_pin_set_dt(&backlight, 0);
#endif
		(void)display_blanking_on(display_device);
		return -ENOMEM;
	}
	ui_init(features, &status);
	ui_status_init(&status);
	ui_initialized = true;
	ui_open(UI_PAGE_SPLASH);
	startup_timer = lv_timer_create(startup_expired, 1200, NULL);
	if (startup_timer == NULL) {
		ui_show_startup();
	} else {
		lv_timer_set_repeat_count(startup_timer, 1);
	}
	refresh_confirmation();
#endif
	lvgl_unlock();

	return 0;
}

bool app_display_ready(void)
{
	return display_ready;
}

#else

int app_init_display(void)
{
	return 0;
}

bool app_display_ready(void)
{
	return false;
}

#endif
