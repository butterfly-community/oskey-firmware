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

#include "ui.h"

#ifdef CONFIG_OSKEY_LVGL_BENCHMARK
#include <lv_demos.h>
#endif

static bool ui_ready;
static lv_timer_t *startup_timer;
static struct app_display_status status = {
	.wifi = IS_ENABLED(CONFIG_OSKEY_WIFI) ? APP_DISPLAY_WIFI_DISCONNECTED
					      : APP_DISPLAY_WIFI_DISABLED,
	.bluetooth = IS_ENABLED(CONFIG_OSKEY_BLUETOOTH) ? APP_DISPLAY_BLUETOOTH_IDLE
							: APP_DISPLAY_BLUETOOTH_DISABLED,
	.usb = IS_ENABLED(CONFIG_OSKEY_USB) ? APP_DISPLAY_USB_DISCONNECTED
					    : APP_DISPLAY_USB_DISABLED,
};

#ifndef CONFIG_OSKEY_LVGL_BENCHMARK
static void startup_expired(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	startup_timer = NULL;
	ui_show_startup();
}
#endif

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

int app_init_display(const struct app_display_startup *startup)
{
	if (startup == NULL) {
		return -EINVAL;
	}

	const struct device *display_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_device)) {
		return -ENODEV;
	}
	int ret;

#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);
	if (!gpio_is_ready_dt(&backlight)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}
#endif

	lvgl_lock();
#ifdef CONFIG_OSKEY_LVGL_BENCHMARK
	lv_demo_benchmark();
#else
	ui_init(startup);
	ui_status_init(&status);
	ui_open(UI_PAGE_SPLASH);
	startup_timer = lv_timer_create(startup_expired, 1200, NULL);
	if (startup_timer == NULL) {
		ui_show_startup();
	} else {
		lv_timer_set_repeat_count(startup_timer, 1);
	}
	ui_ready = true;
#endif
	lvgl_unlock();

	ret = display_blanking_off(display_device);
	return ret == -ENOSYS ? 0 : ret;
}

void app_display_message(DisplayAction action, AppError error, uint32_t value, const uint8_t *data,
			 size_t len)
{
	if (!ui_ready) {
		return;
	}

	lvgl_lock();
	cancel_startup();
	ui_set_busy(false);

	switch (action) {
	case DisplayAction_Ready:
		ui_open(UI_PAGE_HOME);
		break;
	case DisplayAction_Mnemonic:
		ui_wipe(ui.mnemonic, sizeof(ui.mnemonic));
		len = MIN(len, sizeof(ui.mnemonic) - 1);
		if (len > 0 && data != NULL) {
			memcpy(ui.mnemonic, data, len);
		} else {
			len = 0;
		}
		ui.mnemonic[len] = '\0';
		ui_wipe(ui.entropy, sizeof(ui.entropy));
		ui.entropy_bits = 0;
		ui.custom_entropy = false;
		ui_push(UI_PAGE_MNEMONIC);
		break;
	case DisplayAction_Error: {
		char buffer[32];
		const char *text = error_text(error, value, buffer, sizeof(buffer));
		if (error == AppError_UnlockFailed && ui.page == UI_PAGE_LOCKED) {
			ui_input_error(text);
		} else {
			ui_error(text);
		}
		break;
	}
	case DisplayAction_Unspecified:
		break;
	}
	lvgl_unlock();
}

void app_display_confirmation(const struct AppConfirmationView *confirmation)
{
	if (!ui_ready) {
		return;
	}

	lvgl_lock();
	cancel_startup();
	ui_set_busy(false);
	if (confirmation == NULL) {
		ui_dismiss_confirmation();
	} else {
		ui_show_confirmation(confirmation);
	}
	lvgl_unlock();
}

void app_display_wifi_status(enum app_display_wifi_state state)
{
	if (!ui_ready) {
		status.wifi = state;
		return;
	}

	lvgl_lock();
	status.wifi = state;
	ui_status_update(&status);
	lvgl_unlock();
}

void app_display_bluetooth_status(enum app_display_bluetooth_state state)
{
	if (!ui_ready) {
		status.bluetooth = state;
		return;
	}

	lvgl_lock();
	status.bluetooth = state;
	ui_status_update(&status);
	lvgl_unlock();
}

void app_display_usb_status(enum app_display_usb_state state)
{
	if (!ui_ready) {
		status.usb = state;
		return;
	}

	lvgl_lock();
	status.usb = state;
	ui_status_update(&status);
	lvgl_unlock();
}

#else

int app_init_display(const struct app_display_startup *startup)
{
	(void)startup;
	return 0;
}

void app_display_message(DisplayAction action, AppError error, uint32_t value, const uint8_t *data,
			 size_t len)
{
	(void)action;
	(void)error;
	(void)value;
	(void)data;
	(void)len;
}

void app_display_confirmation(const struct AppConfirmationView *confirmation)
{
	(void)confirmation;
}

void app_display_wifi_status(enum app_display_wifi_state state)
{
	(void)state;
}

void app_display_bluetooth_status(enum app_display_bluetooth_state state)
{
	(void)state;
}

void app_display_usb_status(enum app_display_usb_state state)
{
	(void)state;
}

#endif
