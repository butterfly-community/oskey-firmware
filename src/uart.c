#include "uart.h"
#include "bindings.h"
#include "bluetooth/bluetooth.h"

LOG_MODULE_REGISTER(app_uart);

static struct k_work_delayable app_uart_work;
static volatile enum app_uart_transport app_uart_resp_transport = APP_UART_TRANSPORT_UART;

static void app_uart_work_handler(struct k_work *work)
{
	if (app_event_bytes_handle()) {
		k_work_reschedule(&app_uart_work, K_MSEC(10));
	}
}

bool app_uart_handle_rx(enum app_uart_transport transport, const uint8_t *data, size_t len)
{
	if (app_uart_event_rs(data, len)) {
		app_uart_resp_transport = transport;
		k_work_schedule(&app_uart_work, K_NO_WAIT);
		return true;
	}

	return false;
}

void app_uart_rx_handler(const struct device *dev, void *user_data)
{
	uint8_t buf[32];
	uint32_t len = 0;
	uart_irq_update(dev);
	if (uart_irq_rx_ready(dev)) {
		len = uart_fifo_read(dev, buf, sizeof(buf));
		LOG_DBG("UART received %u bytes", len);
		app_uart_handle_rx(APP_UART_TRANSPORT_UART, buf, len);
	}
}

void app_uart_tx_push_array(const uint8_t *data, size_t len)
{
	if (app_uart_resp_transport == APP_UART_TRANSPORT_BLE) {
		int err = oskey_bt_send(data, len);

		if (err) {
			LOG_ERR("Bluetooth response failed (%d)", err);
		}
		return;
	}
	// TODO: irq tx
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(DEV_CONSOLE, data[i]);
	}
}

int app_uart_irq_register()
{
	k_work_init_delayable(&app_uart_work, app_uart_work_handler);

	if (!device_is_ready(DEV_CONSOLE)) {
		return -1;
	}
	uart_irq_callback_user_data_set(DEV_CONSOLE, app_uart_rx_handler, NULL);
	uart_irq_rx_enable(DEV_CONSOLE);
	return 0;
}
