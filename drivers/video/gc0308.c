/*
 * Copyright 2015-2021 Espressif Systems (Shanghai) PTE LTD
 * Copyright (c) 2026 OSKey contributors
 *
 * GC0308 register initialization is adapted from:
 * https://github.com/espressif/esp32-camera/tree/master/sensors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT galaxycore_gc0308

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "gc0308_settings.h"

LOG_MODULE_REGISTER(gc0308, CONFIG_VIDEO_LOG_LEVEL);

#define GC0308_PAGE_SELECT      0xfe
#define GC0308_PRODUCT_ID       0x00
#define GC0308_PRODUCT_ID_VALUE 0x9b
#define GC0308_OUTPUT_FORMAT    0x24
#define GC0308_MIRROR_FLIP      0x14

#define GC0308_WIDTH  320U
#define GC0308_HEIGHT 240U

struct gc0308_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec pwdn;
	struct gpio_dt_spec reset;
	bool hmirror;
};

struct gc0308_data {
	struct video_format format;
};

static const struct video_format_cap gc0308_fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_RGB565X,
		.width_min = GC0308_WIDTH,
		.width_max = GC0308_WIDTH,
		.height_min = GC0308_HEIGHT,
		.height_max = GC0308_HEIGHT,
	},
	{0},
};

static int gc0308_write(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct gc0308_config *config = dev->config;

	return i2c_reg_write_byte_dt(&config->i2c, reg, value);
}

static int gc0308_read(const struct device *dev, uint8_t reg, uint8_t *value)
{
	const struct gc0308_config *config = dev->config;

	return i2c_reg_read_byte_dt(&config->i2c, reg, value);
}

static int gc0308_update(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t current;
	int ret = gc0308_read(dev, reg, &current);

	if (ret < 0) {
		return ret;
	}

	return gc0308_write(dev, reg, (current & ~mask) | (value & mask));
}

static int gc0308_write_defaults(const struct device *dev)
{
	for (size_t i = 0; i < ARRAY_SIZE(gc0308_default_regs); i++) {
		int ret =
			gc0308_write(dev, gc0308_default_regs[i].reg, gc0308_default_regs[i].value);

		if (ret < 0) {
			LOG_ERR("Register 0x%02x write failed: %d", gc0308_default_regs[i].reg,
				ret);
			return ret;
		}
	}

	return 0;
}

static int gc0308_configure_qvga_rgb565(const struct device *dev)
{
	int ret;

	/* Capture the full VGA field, then use the sensor's 1/2 subsampler. */
	const struct gc0308_reg regs[] = {
		{GC0308_PAGE_SELECT, 0x00},
		{0x05, 0x00},
		{0x06, 0x00}, /* row start */
		{0x07, 0x00},
		{0x08, 0x00}, /* column start */
		{0x09, 0x01},
		{0x0a, 0xe8}, /* 480 + 8 */
		{0x0b, 0x02},
		{0x0c, 0x88}, /* 640 + 8 */
		{GC0308_PAGE_SELECT, 0x01},
		{0x54, 0x22},
		{0x56, 0x00},
		{0x57, 0x00},
		{0x58, 0x00},
		{0x59, 0x00},
	};

	for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = gc0308_write(dev, regs[i].reg, regs[i].value);
		if (ret < 0) {
			return ret;
		}
	}

	ret = gc0308_update(dev, 0x53, BIT(7), BIT(7));
	if (ret < 0) {
		return ret;
	}
	ret = gc0308_update(dev, 0x55, BIT(0), BIT(0));
	if (ret < 0) {
		return ret;
	}

	ret = gc0308_write(dev, GC0308_PAGE_SELECT, 0x00);
	if (ret < 0) {
		return ret;
	}
	ret = gc0308_update(dev, GC0308_OUTPUT_FORMAT, 0x0f, 0x06);
	if (ret < 0) {
		return ret;
	}

	const struct gc0308_config *config = dev->config;

	return gc0308_update(dev, GC0308_MIRROR_FLIP, BIT(0) | BIT(1),
			     config->hmirror ? BIT(0) : 0U);
}

static int gc0308_set_format(const struct device *dev, struct video_format *fmt)
{
	struct gc0308_data *data = dev->data;
	int ret;

	if (fmt == NULL) {
		return -EINVAL;
	}
	if (fmt->type != VIDEO_BUF_TYPE_OUTPUT || fmt->pixelformat != VIDEO_PIX_FMT_RGB565X ||
	    fmt->width != GC0308_WIDTH || fmt->height != GC0308_HEIGHT) {
		return -ENOTSUP;
	}

	fmt->pitch = GC0308_WIDTH * 2U;
	fmt->size = fmt->pitch * GC0308_HEIGHT;
	if (memcmp(&data->format, fmt, sizeof(*fmt)) == 0) {
		return 0;
	}

	ret = gc0308_configure_qvga_rgb565(dev);
	if (ret < 0) {
		return ret;
	}
	data->format = *fmt;
	return 0;
}

static int gc0308_get_format(const struct device *dev, struct video_format *fmt)
{
	struct gc0308_data *data = dev->data;

	*fmt = data->format;
	return 0;
}

static int gc0308_get_caps(const struct device *dev, struct video_caps *caps)
{
	ARG_UNUSED(dev);

	if (caps->type != VIDEO_BUF_TYPE_OUTPUT) {
		return -ENOTSUP;
	}

	caps->format_caps = gc0308_fmts;
	return 0;
}

static int gc0308_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enable);

	return type == VIDEO_BUF_TYPE_OUTPUT ? 0 : -ENOTSUP;
}

static DEVICE_API(video, gc0308_api) = {
	.set_format = gc0308_set_format,
	.get_format = gc0308_get_format,
	.set_stream = gc0308_set_stream,
	.get_caps = gc0308_get_caps,
};

static int gc0308_power_up(const struct device *dev)
{
	const struct gc0308_config *config = dev->config;
	int ret;

	if (config->pwdn.port != NULL) {
		if (!gpio_is_ready_dt(&config->pwdn)) {
			LOG_ERR("PWDN GPIO is not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&config->pwdn, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	k_msleep(10);

	if (config->reset.port != NULL) {
		if (!gpio_is_ready_dt(&config->reset)) {
			LOG_ERR("Reset GPIO is not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return ret;
		}
		k_msleep(10);
		ret = gpio_pin_set_dt(&config->reset, 0);
		if (ret < 0) {
			return ret;
		}
		k_msleep(30);
	}
	return 0;
}

static int gc0308_init(const struct device *dev)
{
	const struct gc0308_config *config = dev->config;
	struct video_format format = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_RGB565X,
		.width = GC0308_WIDTH,
		.height = GC0308_HEIGHT,
	};
	uint8_t product_id;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("SCCB bus is not ready");
		return -ENODEV;
	}

	ret = gc0308_power_up(dev);
	if (ret < 0) {
		return ret;
	}

	ret = gc0308_write(dev, GC0308_PAGE_SELECT, 0x00);
	if (ret < 0) {
		return ret;
	}
	ret = gc0308_read(dev, GC0308_PRODUCT_ID, &product_id);
	if (ret < 0) {
		LOG_ERR("Product ID read failed: %d", ret);
		return ret;
	}
	if (product_id != GC0308_PRODUCT_ID_VALUE) {
		LOG_ERR("Unexpected product ID 0x%02x", product_id);
		return -ENODEV;
	}

	ret = gc0308_write(dev, GC0308_PAGE_SELECT, 0xf0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(80);

	ret = gc0308_write_defaults(dev);
	if (ret < 0) {
		return ret;
	}
	k_msleep(80);

	ret = gc0308_set_format(dev, &format);
	if (ret < 0) {
		LOG_ERR("QVGA RGB565 configuration failed: %d", ret);
		return ret;
	}

	LOG_INF("GC0308 detected (PID 0x%02x), QVGA RGB565X", product_id);
	return 0;
}

#define GC0308_INIT(inst)                                                                          \
	static struct gc0308_data gc0308_data_##inst;                                              \
	static const struct gc0308_config gc0308_config_##inst = {                                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.pwdn = GPIO_DT_SPEC_INST_GET_OR(inst, pwdn_gpios, {0}),                           \
		.reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                         \
		.hmirror = DT_INST_PROP_OR(inst, h_mirror, false),                                 \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, gc0308_init, NULL, &gc0308_data_##inst, &gc0308_config_##inst, \
			      POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY, &gc0308_api);

DT_INST_FOREACH_STATUS_OKAY(GC0308_INIT)
