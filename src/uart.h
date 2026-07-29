#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

#if DT_NODE_HAS_STATUS(DT_ALIAS(app_uart), okay)

#define DEV_CONSOLE DEVICE_DT_GET(DT_ALIAS(app_uart))

#else

#define DEV_CONSOLE DEVICE_DT_GET(DT_CHOSEN(zephyr_console))

#endif

void app_uart_send(const uint8_t *data, size_t len);

int app_uart_irq_register(void);

#endif
