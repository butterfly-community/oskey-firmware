#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include "wrapper.h"

#if DT_NODE_HAS_STATUS(DT_ALIAS(app_uart), okay)

#define DEV_CONSOLE DEVICE_DT_GET(DT_ALIAS(app_uart))

#else

#define DEV_CONSOLE DEVICE_DT_GET(DT_CHOSEN(zephyr_console))

#endif

enum app_uart_transport {
	APP_UART_TRANSPORT_UART = 0,
	APP_UART_TRANSPORT_BLE = 1,
};

extern struct k_work app_uart_work;

void app_uart_rx_handler(const struct device *dev, void *user_data);

void app_uart_tx_push_array(const uint8_t *data, size_t len);

int app_uart_irq_register();

void app_uart_work_handler(struct k_work *work);

bool app_uart_handle_rx(enum app_uart_transport transport, const uint8_t *data, size_t len);

#endif
