#include "uart.h"
#include "bindings.h"
#include "bluetooth.h"

void app_uart_work_handler(struct k_work *work)
{
	app_event_bytes_handle();
}

void app_uart_rx_handler(const struct device *dev, void *user_data)
{
	uint8_t buf[32];
	uint32_t len = 0;
	if (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		len = uart_fifo_read(dev, buf, sizeof(buf));
		printf("uart rx %u bytes\n", len);
		if (app_uart_event_rs(buf, len)) {
			k_work_submit(&app_uart_work);
		}
	}
}

void app_uart_tx_push_array(const uint8_t *data, size_t len)
{
	bt_nus_send_bytes(data, len);
	// TODO: irq tx
	for (int i = 0; i < len; i++) {
		uart_poll_out(DEV_CONSOLE, data[i]);
	}
}

int app_uart_irq_register()
{
	k_work_init(&app_uart_work, app_uart_work_handler);

	if (!device_is_ready(DEV_CONSOLE)) {
		return -1;
	}
	uart_irq_callback_user_data_set(DEV_CONSOLE, app_uart_rx_handler, NULL);
	uart_irq_rx_enable(DEV_CONSOLE);
	return 0;
}
