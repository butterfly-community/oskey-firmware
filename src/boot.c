#include "boot.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "app.h"
#include "storage.h"

LOG_MODULE_REGISTER(app_boot);

#define UPDATE_UART DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_mcumgr))

static void reboot_for_update(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(update_reboot_work, reboot_for_update);

int confirm_mcuboot_img(void)
{
	if (!boot_is_img_confirmed()) {
		return boot_write_img_confirmed();
	}
	return 0;
}

bool app_update_mode_take(void)
{
	uint8_t enabled;
	int ret = storage_read(&enabled, sizeof(enabled), storage_ids.firmware_update);

	if (ret == sizeof(enabled) && enabled != 0) {
		if (storage_delete(storage_ids.firmware_update) == 0) {
			LOG_INF("Firmware update mode active on UART");
			return true;
		}
		LOG_ERR("Failed to consume firmware update mode");
	}

	uart_irq_rx_disable(UPDATE_UART);
	return false;
}

bool app_update_request(void)
{
	uint8_t enabled = 1;

	if (storage_write(&enabled, sizeof(enabled), storage_ids.firmware_update) < 0) {
		return false;
	}

	k_work_reschedule(&update_reboot_work, K_MSEC(250));
	return true;
}
