#include "uart.h"
#include "bindings.h"

static uint8_t *rx_buf = NULL;
static uint32_t rx_len = 0;

void app_uart_work_handler(struct k_work *work)
{
	event_bytes_handle(rx_buf, rx_len);
	rx_len = 0;
}

void app_uart_rx_handler(const struct device *dev, void *user_data)
{
	//TODO: submit data use k_msgq_get
	uint8_t buf[32];
	uint32_t len = 0;
	if (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		len = uart_fifo_read(dev, buf, sizeof(buf));
		memcpy(rx_buf + rx_len, buf, len);
		rx_len += len;
		if (rx_len >= 5) {
			uint16_t data_len = (rx_buf[3] << 8) | rx_buf[4];
			if (rx_buf[0] == 0xE2 && rx_buf[1] == 0x82 && rx_buf[2] == 0xBF) {
				if (data_len > 0 && rx_len >= (5 + data_len)) {
					k_work_submit(&app_uart_work);
				}
			}
		}
	}
}

void app_uart_tx_push_array(const uint8_t *data, size_t len)
{
	// TODO: irq tx
	for (int i = 0; i < len; i++) {
		uart_poll_out(DEV_CONSOLE, data[i]);
	}
}

int app_uart_irq_register()
{
	if (!device_is_ready(DEV_CONSOLE)) {
		return -1;
	}
	rx_buf = k_malloc(12288 + 7);
	uart_irq_callback_user_data_set(DEV_CONSOLE, app_uart_rx_handler, NULL);
	uart_irq_rx_enable(DEV_CONSOLE);
	return 0;
}
