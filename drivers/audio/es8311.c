/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 OSKey contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted for Zephyr from Espressif's es8311 component version 0.0.2.
 * Modified by OSKey contributors in 2026 to use Zephyr's audio codec API,
 * devicetree configuration, and NS4150B amplifier control.
 */

#define DT_DRV_COMPAT everest_es8311

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(es8311, LOG_LEVEL_INF);

/* Register map (see ESP-ADF es8311_reg.h). */
#define ES8311_RESET_REG00       0x00
#define ES8311_CLK_MANAGER_REG01 0x01
#define ES8311_CLK_MANAGER_REG02 0x02
#define ES8311_CLK_MANAGER_REG03 0x03
#define ES8311_CLK_MANAGER_REG04 0x04
#define ES8311_CLK_MANAGER_REG05 0x05
#define ES8311_CLK_MANAGER_REG06 0x06
#define ES8311_CLK_MANAGER_REG07 0x07
#define ES8311_CLK_MANAGER_REG08 0x08
#define ES8311_SDPIN_REG09       0x09
#define ES8311_SDPOUT_REG0A      0x0A
#define ES8311_SYSTEM_REG0B      0x0B
#define ES8311_SYSTEM_REG0C      0x0C
#define ES8311_SYSTEM_REG0D      0x0D
#define ES8311_SYSTEM_REG0E      0x0E
#define ES8311_SYSTEM_REG10      0x10
#define ES8311_SYSTEM_REG11      0x11
#define ES8311_SYSTEM_REG12      0x12
#define ES8311_SYSTEM_REG13      0x13
#define ES8311_SYSTEM_REG14      0x14
#define ES8311_ADC_REG15         0x15
#define ES8311_ADC_REG16         0x16
#define ES8311_ADC_REG17         0x17
#define ES8311_ADC_REG1B         0x1B
#define ES8311_ADC_REG1C         0x1C
#define ES8311_DAC_REG31         0x31
#define ES8311_DAC_REG32         0x32
#define ES8311_DAC_REG37         0x37
#define ES8311_GPIO_REG44        0x44
#define ES8311_GP_REG45          0x45

struct es8311_coeff {
	uint32_t mclk;
	uint32_t rate;
	uint8_t pre_div;
	uint8_t pre_multi;
	uint8_t adc_div;
	uint8_t dac_div;
	uint8_t fs_mode;
	uint8_t lrck_h;
	uint8_t lrck_l;
	uint8_t bclk_div;
	uint8_t adc_osr;
	uint8_t dac_osr;
};

/* Clock divider coefficients for MCLK = 256 x sample rate. */
static const struct es8311_coeff es8311_coeffs[] = {
	{2048000U,  8000U,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
	{2822400U,  11025U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
	{4096000U,  16000U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
	{5644800U,  22050U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
	{6144000U,  24000U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
	{8192000U,  32000U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
	{11289600U, 44100U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
	{12288000U, 48000U, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

struct es8311_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec pa_en;
};

struct es8311_data {
	uint8_t volume; /* Cached 0-100 DAC output volume. */
	bool started;
};

static int es8311_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct es8311_config *config = dev->config;

	return i2c_reg_write_byte_dt(&config->i2c, reg, value);
}

static int es8311_read_reg(const struct device *dev, uint8_t reg, uint8_t *value)
{
	const struct es8311_config *config = dev->config;

	return i2c_reg_read_byte_dt(&config->i2c, reg, value);
}

static const struct es8311_coeff *es8311_find_coeff(uint32_t mclk, uint32_t rate)
{
	for (size_t i = 0; i < ARRAY_SIZE(es8311_coeffs); i++) {
		if (es8311_coeffs[i].mclk == mclk && es8311_coeffs[i].rate == rate) {
			return &es8311_coeffs[i];
		}
	}
	return NULL;
}

static int es8311_set_sample_rate(const struct device *dev, uint32_t mclk, uint32_t rate)
{
	const struct es8311_coeff *coeff = es8311_find_coeff(mclk, rate);

	if (coeff == NULL) {
		LOG_ERR("Unsupported sample rate %u", rate);
		return -ENOTSUP;
	}

	uint8_t regv;
	int ret = 0;

	ret |= es8311_read_reg(dev, ES8311_CLK_MANAGER_REG02, &regv);
	regv &= 0x07;
	regv |= (coeff->pre_div - 1) << 5;
	uint8_t multiplier = 0;

	switch (coeff->pre_multi) {
	case 2:
		multiplier = 1;
		break;
	case 4:
		multiplier = 2;
		break;
	case 8:
		multiplier = 3;
		break;
	default:
		break;
	}
	regv |= multiplier << 3;
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG02, regv);

	regv = ((coeff->adc_div - 1) << 4) | (coeff->dac_div - 1);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG05, regv);

	ret |= es8311_read_reg(dev, ES8311_CLK_MANAGER_REG03, &regv);
	regv &= 0x80;
	regv |= (coeff->fs_mode << 6) | coeff->adc_osr;
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG03, regv);

	ret |= es8311_read_reg(dev, ES8311_CLK_MANAGER_REG04, &regv);
	regv &= 0x80;
	regv |= coeff->dac_osr;
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG04, regv);

	ret |= es8311_read_reg(dev, ES8311_CLK_MANAGER_REG07, &regv);
	regv &= 0xC0;
	regv |= coeff->lrck_h;
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG07, regv);

	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG08, coeff->lrck_l);

	ret |= es8311_read_reg(dev, ES8311_CLK_MANAGER_REG06, &regv);
	regv &= 0xE0;
	regv |= coeff->bclk_div - 1;
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG06, regv);

	if (ret < 0) {
		LOG_ERR("Failed to configure sample rate %u", rate);
	}
	return ret;
}

static int es8311_init_regs(const struct device *dev)
{
	uint8_t regv = 0;
	int ret = 0;

	/* Bring the chip out of power-down and reset it. */
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG0D, 0xFA);
	ret |= es8311_write_reg(dev, ES8311_GPIO_REG44, 0x08);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG01, 0x30);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG02, 0x00);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG03, 0x10);
	ret |= es8311_write_reg(dev, ES8311_ADC_REG16, 0x24);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG04, 0x10);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG05, 0x00);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG0B, 0x00);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG0C, 0x00);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG10, 0x1F);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG11, 0x7F);
	ret |= es8311_write_reg(dev, ES8311_RESET_REG00, 0x80);

	/* Codec in I2S slave mode. */
	ret |= es8311_read_reg(dev, ES8311_RESET_REG00, &regv);
	regv &= 0xBF;
	ret |= es8311_write_reg(dev, ES8311_RESET_REG00, regv);

	/* MCLK from the MCLK pin, not inverted, codec clock enabled. */
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG01, 0x3F);

	/* BCLK not inverted. */
	ret |= es8311_read_reg(dev, ES8311_CLK_MANAGER_REG06, &regv);
	regv &= ~0x20;
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG06, regv);

	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG13, 0x10);
	ret |= es8311_write_reg(dev, ES8311_ADC_REG1B, 0x0A);
	ret |= es8311_write_reg(dev, ES8311_ADC_REG1C, 0x6A);
	/* No internal DAC->ADC reference; the board routes AEC in hardware. */
	ret |= es8311_write_reg(dev, ES8311_GPIO_REG44, 0x08);

	/* Serial port: standard I2S, 16-bit. */
	ret |= es8311_read_reg(dev, ES8311_SDPIN_REG09, &regv);
	regv = (regv & 0xFC) | 0x0C;
	ret |= es8311_write_reg(dev, ES8311_SDPIN_REG09, regv);
	ret |= es8311_read_reg(dev, ES8311_SDPOUT_REG0A, &regv);
	regv = (regv & 0xFC) | 0x0C;
	ret |= es8311_write_reg(dev, ES8311_SDPOUT_REG0A, regv);

	if (ret < 0) {
		LOG_ERR("Register init failed: %d", ret);
	}
	return ret;
}

static int es8311_dac_power_up(const struct device *dev)
{
	uint8_t regv = 0;
	int ret = 0;

	/* Keep I2S slave mode and the codec clock running. */
	ret |= es8311_read_reg(dev, ES8311_RESET_REG00, &regv);
	regv &= 0xBF;
	ret |= es8311_write_reg(dev, ES8311_RESET_REG00, regv);
	ret |= es8311_write_reg(dev, ES8311_CLK_MANAGER_REG01, 0x3F);

	/* Enable the DAC serial port and power up the DAC path. */
	ret |= es8311_read_reg(dev, ES8311_SDPIN_REG09, &regv);
	regv &= ~0x40;
	ret |= es8311_write_reg(dev, ES8311_SDPIN_REG09, regv);
	ret |= es8311_write_reg(dev, ES8311_ADC_REG17, 0xBF);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG0E, 0x02);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG12, 0x00);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG14, 0x1A);
	ret |= es8311_write_reg(dev, ES8311_SYSTEM_REG0D, 0x01);
	ret |= es8311_write_reg(dev, ES8311_ADC_REG15, 0x40);
	ret |= es8311_write_reg(dev, ES8311_DAC_REG37, 0x08);
	ret |= es8311_write_reg(dev, ES8311_GP_REG45, 0x00);

	/* Unmute the DAC. */
	ret |= es8311_read_reg(dev, ES8311_DAC_REG31, &regv);
	regv &= 0x9F;
	ret |= es8311_write_reg(dev, ES8311_DAC_REG31, regv);

	if (ret < 0) {
		LOG_ERR("DAC power-up failed: %d", ret);
	}
	return ret;
}

static int es8311_set_volume(const struct device *dev, uint8_t volume)
{
	if (volume > 100) {
		volume = 100;
	}
	/* Same 0-100 -> register mapping as the ESP-ADF driver. */
	uint8_t reg = (uint8_t)(volume * 255U / 100U);

	return es8311_write_reg(dev, ES8311_DAC_REG32, reg);
}

static int es8311_apply_properties(const struct device *dev)
{
	struct es8311_data *data = dev->data;

	return es8311_set_volume(dev, data->volume);
}

static int es8311_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	int ret;

	if (cfg == NULL || cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		return -ENOTSUP;
	}
	if (cfg->dai_route != AUDIO_ROUTE_PLAYBACK &&
	    cfg->dai_route != AUDIO_ROUTE_PLAYBACK_CAPTURE) {
		return -ENOTSUP;
	}
	if (cfg->dai_cfg.i2s.frame_clk_freq == 0) {
		return -EINVAL;
	}

	ret = es8311_init_regs(dev);
	if (ret < 0) {
		return ret;
	}
	ret = es8311_set_sample_rate(dev, cfg->mclk_freq, cfg->dai_cfg.i2s.frame_clk_freq);
	if (ret < 0) {
		return ret;
	}

	/* Re-apply any cached properties (e.g. volume). */
	return es8311_apply_properties(dev);
}

static int es8311_start(const struct device *dev, audio_dai_dir_t dir)
{
	const struct es8311_config *config = dev->config;
	struct es8311_data *data = dev->data;
	int ret;

	if ((dir & AUDIO_DAI_DIR_TX) == 0) {
		return 0; /* Capture is not supported. */
	}
	if (data->started) {
		return 0;
	}

	ret = es8311_dac_power_up(dev);
	if (ret < 0) {
		return ret;
	}

	if (config->pa_en.port != NULL) {
		if (!gpio_is_ready_dt(&config->pa_en)) {
			LOG_ERR("Amplifier enable GPIO is not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&config->pa_en, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Amplifier enable failed: %d", ret);
			return ret;
		}
	}

	data->started = true;
	return 0;
}

static int es8311_stop(const struct device *dev, audio_dai_dir_t dir)
{
	const struct es8311_config *config = dev->config;
	struct es8311_data *data = dev->data;

	if ((dir & AUDIO_DAI_DIR_TX) == 0) {
		return 0;
	}

	if (config->pa_en.port != NULL) {
		gpio_pin_set_dt(&config->pa_en, 0);
	}
	data->started = false;
	return 0;
}

static void es8311_start_output(const struct device *dev)
{
	(void)es8311_start(dev, AUDIO_DAI_DIR_TX);
}

static void es8311_stop_output(const struct device *dev)
{
	(void)es8311_stop(dev, AUDIO_DAI_DIR_TX);
}

static int es8311_set_property(const struct device *dev, audio_property_t property,
			       audio_channel_t channel, audio_property_value_t value)
{
	struct es8311_data *data = dev->data;

	if (channel != AUDIO_CHANNEL_ALL) {
		return -ENOTSUP;
	}
	if (property != AUDIO_PROPERTY_OUTPUT_VOLUME) {
		return -ENOTSUP;
	}
	if (value.vol < 0) {
		value.vol = 0;
	}
	if (value.vol > 100) {
		value.vol = 100;
	}
	data->volume = (uint8_t)value.vol;

	return es8311_set_volume(dev, data->volume);
}

static DEVICE_API(audio_codec, es8311_api) = {
	.configure = es8311_configure,
	.start_output = es8311_start_output,
	.stop_output = es8311_stop_output,
	.set_property = es8311_set_property,
	.apply_properties = es8311_apply_properties,
	.start = es8311_start,
	.stop = es8311_stop,
};

static int es8311_init(const struct device *dev)
{
	const struct es8311_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus is not ready");
		return -ENODEV;
	}
	return 0;
}

#define ES8311_INIT(inst)                                                                          \
	static struct es8311_data es8311_data_##inst;                                              \
	static const struct es8311_config es8311_config_##inst = {                                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.pa_en = GPIO_DT_SPEC_INST_GET_OR(inst, pa_en_gpios, {0}),                         \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, es8311_init, NULL, &es8311_data_##inst,                        \
			      &es8311_config_##inst, POST_KERNEL, CONFIG_AUDIO_ES8311_INIT_PRIORITY, \
			      &es8311_api);

DT_INST_FOREACH_STATUS_OKAY(ES8311_INIT)
