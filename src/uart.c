#define _DEFAULT_SOURCE

#include "uart.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <zephyr/logging/log.h>

#include "bus.h"

LOG_MODULE_REGISTER(app_uart);

#define APP_UART_RETRY_DELAY K_MSEC(2)

static uint8_t pending[32];
static size_t pending_len;

static void app_uart_rx_resume(struct k_work *work)
{
	struct k_work_delayable *resume_work = k_work_delayable_from_work(work);

	if (pending_len > 0) {
		struct TransportRoute route = {.transport = Transport_Uart};
		int ret = app_core_submit_protocol(route, pending, pending_len, K_NO_WAIT);

		if (ret != 0) {
			k_work_reschedule(resume_work, APP_UART_RETRY_DELAY);
			return;
		}
		explicit_bzero(pending, pending_len);
		pending_len = 0;
	}
	uart_irq_rx_enable(DEV_CONSOLE);
}

static K_WORK_DELAYABLE_DEFINE(app_uart_rx_resume_work, app_uart_rx_resume);

static void app_uart_rx_handler(const struct device *dev, void *user_data)
{
	uint8_t buf[32];
	int len;

	uart_irq_update(dev);
	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while ((len = uart_fifo_read(dev, buf, sizeof(buf))) > 0) {
		struct TransportRoute route = {.transport = Transport_Uart};
		int ret = app_core_submit_protocol(route, buf, len, K_NO_WAIT);

		if (ret == 0) {
			LOG_DBG("UART received %d bytes", len);
		} else {
			memcpy(pending, buf, len);
			pending_len = len;
			uart_irq_rx_disable(dev);
			k_work_reschedule(&app_uart_rx_resume_work, APP_UART_RETRY_DELAY);
		}

		explicit_bzero(buf, len);
		if (ret != 0) {
			return;
		}
	}
}

int app_uart_send(const uint8_t *data, size_t len)
{
	if (data == NULL && len > 0) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(DEV_CONSOLE, data[i]);
	}
	return 0;
}

int app_uart_irq_register(void)
{
	if (!device_is_ready(DEV_CONSOLE)) {
		return -1;
	}
	int ret = uart_irq_callback_user_data_set(DEV_CONSOLE, app_uart_rx_handler, NULL);
	if (ret) {
		return ret;
	}

	k_work_reschedule(&app_uart_rx_resume_work, K_NO_WAIT);
	return 0;
}
