#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include "port/uart.h"
#include "storage/storage.h"
#include "test.h"

void cs_random(void *dst, size_t len);

int main(void)
{
	// test_wallet();

	storage_init();

	k_work_init(&app_uart_work, app_uart_work_handler);

	app_uart_irq_register();

	while (true) {
		k_sleep(K_MSEC(100));
	}
	return 0;
}

void cs_random(void *dst, size_t len)
{
	sys_csrand_get(dst, len);
}

void rust_panic_wrap(void)
{
	k_panic();
}
