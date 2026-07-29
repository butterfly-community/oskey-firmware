#include "uart.h"

#include <zephyr/logging/log.h>

#include "message.h"

LOG_MODULE_REGISTER(app_uart);

static void app_uart_rx_handler(const struct device *dev, void *user_data)
{
	uint8_t buf[32];
	int len;

	uart_irq_update(dev);
	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while ((len = uart_fifo_read(dev, buf, sizeof(buf))) > 0) {
		if (app_message_submit(AppMessageSource_Uart, AppMessageAction_External, 0, buf,
				       len, NULL, 0)) {
			LOG_DBG("UART received %d bytes", len);
		} else {
			LOG_ERR("Failed to queue %d UART bytes", len);
		}
	}
}

void app_uart_send(const uint8_t *data, size_t len)
{
	// TODO: irq tx
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(DEV_CONSOLE, data[i]);
	}
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

	uart_irq_rx_enable(DEV_CONSOLE);
	return 0;
}
