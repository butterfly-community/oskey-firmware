/* SPDX-License-Identifier: MPL-2.0 */
#include "sm_i2c.h"
#include "sm_timer.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

#if DT_HAS_CHOSEN(oskey_secure_element)
#define NXP_NODE DT_CHOSEN(oskey_secure_element)
static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(NXP_NODE);
#if DT_NODE_HAS_PROP(NXP_NODE, enable_gpios)
static const struct gpio_dt_spec enable = GPIO_DT_SPEC_GET(NXP_NODE, enable_gpios);
#endif
#endif

i2c_error_t axI2CInit(void **ctx, const char *name)
{
	(void)name;
	*ctx = NULL;
#if DT_HAS_CHOSEN(oskey_secure_element)
	if (!i2c_is_ready_dt(&bus)) {
		return I2C_FAILED;
	}
#if DT_NODE_HAS_PROP(NXP_NODE, enable_gpios)
	if (!gpio_is_ready_dt(&enable) || gpio_pin_configure_dt(&enable, GPIO_OUTPUT_ACTIVE)) {
		return I2C_FAILED;
	}
	k_msleep(10);
#endif
	*ctx = (void *)&bus;
	return I2C_OK;
#else
	return I2C_FAILED;
#endif
}

void axI2CTerm(void *ctx, int mode)
{
	(void)ctx;
	(void)mode;
}
i2c_error_t axI2CWrite(void *ctx, unsigned char bus_id, unsigned char addr, unsigned char *data,
		       unsigned short len)
{
	(void)bus_id;
	(void)addr;
	return ctx && !i2c_write_dt(ctx, data, len) ? I2C_OK : I2C_FAILED;
}
i2c_error_t axI2CRead(void *ctx, unsigned char bus_id, unsigned char addr, unsigned char *data,
		      unsigned short len)
{
	(void)bus_id;
	(void)addr;
	return ctx && !i2c_read_dt(ctx, data, len) ? I2C_OK : I2C_FAILED;
}
uint32_t sm_initSleep(void)
{
	return 0;
}
void sm_sleep(uint32_t ms)
{
	k_msleep(ms);
}
void sm_usleep(uint32_t us)
{
	k_usleep(us);
}
