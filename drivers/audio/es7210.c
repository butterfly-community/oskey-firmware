/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Everest ES7210 audio ADC driver. The register programming model and
 * power sequence follow the ES7210 data sheet and Espressif's ES7210
 * driver. All board wiring and channel policy are supplied by devicetree.
 */

#define DT_DRV_COMPAT everest_es7210

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(es7210, LOG_LEVEL_INF);

#define ES7210_RESET_REG00           0x00
#define ES7210_CLOCK_OFF_REG01       0x01
#define ES7210_MAINCLK_REG02         0x02
#define ES7210_MASTER_CLK_REG03      0x03
#define ES7210_LRCK_DIVH_REG04       0x04
#define ES7210_LRCK_DIVL_REG05       0x05
#define ES7210_POWER_DOWN_REG06      0x06
#define ES7210_OSR_REG07             0x07
#define ES7210_MODE_CONFIG_REG08     0x08
#define ES7210_TIME_CONTROL0_REG09   0x09
#define ES7210_TIME_CONTROL1_REG0A   0x0A
#define ES7210_SDP_INTERFACE1_REG11  0x11
#define ES7210_SDP_INTERFACE2_REG12  0x12
#define ES7210_ADC34_MUTERANGE_REG14 0x14
#define ES7210_ADC12_MUTERANGE_REG15 0x15
#define ES7210_ADC34_HPF2_REG20      0x20
#define ES7210_ADC34_HPF1_REG21      0x21
#define ES7210_ADC12_HPF1_REG22      0x22
#define ES7210_ADC12_HPF2_REG23      0x23
#define ES7210_ANALOG_REG40          0x40
#define ES7210_MIC12_BIAS_REG41      0x41
#define ES7210_MIC34_BIAS_REG42      0x42
#define ES7210_MIC1_GAIN_REG43       0x43
#define ES7210_MIC1_POWER_REG47      0x47
#define ES7210_MIC2_POWER_REG48      0x48
#define ES7210_MIC3_POWER_REG49      0x49
#define ES7210_MIC4_POWER_REG4A      0x4A
#define ES7210_MIC12_POWER_REG4B     0x4B
#define ES7210_MIC34_POWER_REG4C     0x4C

#define ES7210_MIC1           BIT(0)
#define ES7210_MIC2           BIT(1)
#define ES7210_CLOCK_ON_VALUE 0x34
#define ES7210_GAIN_MAX       14U

struct es7210_coeff {
	uint32_t mclk;
	uint32_t rate;
	uint8_t adc_div;
	uint8_t dll;
	uint8_t doubler;
	uint8_t osr;
	uint8_t lrck_h;
	uint8_t lrck_l;
};

static const struct es7210_coeff es7210_coeffs[] = {
	{4096000U, 8000U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{11289600U, 11025U, 0x02, 0x01, 0x00, 0x20, 0x01, 0x00},
	{4096000U, 16000U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
	{11289600U, 22050U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{12288000U, 24000U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{8192000U, 32000U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
	{16384000U, 32000U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{11289600U, 44100U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
	{12288000U, 48000U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
};

struct es7210_config {
	struct i2c_dt_spec i2c;
	uint8_t input_channel_mask;
	uint8_t mic_gain;
};

struct es7210_data {
	uint8_t gain;
	bool muted;
	bool configured;
	bool started;
};

static int es7210_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct es7210_config *config = dev->config;
	int ret = i2c_reg_write_byte_dt(&config->i2c, reg, value);

	if (ret < 0) {
		LOG_ERR("Register 0x%02x write failed: %d", reg, ret);
	}
	return ret;
}

static int es7210_update_reg(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t value)
{
	const struct es7210_config *config = dev->config;
	int ret = i2c_reg_update_byte_dt(&config->i2c, reg, mask, value);

	if (ret < 0) {
		LOG_ERR("Register 0x%02x update failed: %d", reg, ret);
	}
	return ret;
}

static const struct es7210_coeff *es7210_find_coeff(uint32_t mclk, uint32_t rate)
{
	for (size_t i = 0; i < ARRAY_SIZE(es7210_coeffs); i++) {
		if (es7210_coeffs[i].mclk == mclk && es7210_coeffs[i].rate == rate) {
			return &es7210_coeffs[i];
		}
	}
	return NULL;
}

static int es7210_set_sample_rate(const struct device *dev, uint32_t mclk, uint32_t rate)
{
	const struct es7210_coeff *coeff = es7210_find_coeff(mclk, rate);

	if (coeff == NULL) {
		LOG_ERR("Unsupported MCLK %u Hz and sample rate %u Hz", mclk, rate);
		return -ENOTSUP;
	}

	int ret = es7210_write_reg(dev, ES7210_MAINCLK_REG02,
				   coeff->adc_div | (coeff->doubler << 6) | (coeff->dll << 7));

	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_OSR_REG07, coeff->osr);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_LRCK_DIVH_REG04, coeff->lrck_h);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_LRCK_DIVL_REG05, coeff->lrck_l);
	}
	return ret;
}

static int es7210_set_gain(const struct device *dev, uint8_t gain)
{
	const struct es7210_config *config = dev->config;
	struct es7210_data *data = dev->data;
	int ret = 0;

	gain = MIN(gain, ES7210_GAIN_MAX);
	for (uint8_t channel = 0; channel < 4 && ret == 0; channel++) {
		uint8_t enabled = config->input_channel_mask & BIT(channel);
		uint8_t value = enabled ? (0x10 | gain) : 0x00;

		ret = es7210_write_reg(dev, ES7210_MIC1_GAIN_REG43 + channel, value);
	}
	if (ret == 0) {
		data->gain = gain;
	}
	return ret;
}

static int es7210_set_mute(const struct device *dev, bool muted)
{
	struct es7210_data *data = dev->data;
	uint8_t value = muted ? 0x03 : 0x00;
	int ret = es7210_update_reg(dev, ES7210_ADC12_MUTERANGE_REG15, 0x03, value);

	if (ret == 0) {
		ret = es7210_update_reg(dev, ES7210_ADC34_MUTERANGE_REG14, 0x03, value);
	}
	if (ret == 0) {
		data->muted = muted;
	}
	return ret;
}

static int es7210_power_down(const struct device *dev)
{
	static const struct {
		uint8_t reg;
		uint8_t value;
	} sequence[] = {
		{ES7210_MIC1_POWER_REG47, 0xFF},  {ES7210_MIC2_POWER_REG48, 0xFF},
		{ES7210_MIC3_POWER_REG49, 0xFF},  {ES7210_MIC4_POWER_REG4A, 0xFF},
		{ES7210_MIC12_POWER_REG4B, 0xFF}, {ES7210_MIC34_POWER_REG4C, 0xFF},
		{ES7210_ANALOG_REG40, 0xC0},      {ES7210_CLOCK_OFF_REG01, 0x7F},
		{ES7210_POWER_DOWN_REG06, 0x07},
	};

	for (size_t i = 0; i < ARRAY_SIZE(sequence); i++) {
		int ret = es7210_write_reg(dev, sequence[i].reg, sequence[i].value);

		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

static int es7210_apply_properties(const struct device *dev)
{
	struct es7210_data *data = dev->data;
	int ret = es7210_set_gain(dev, data->gain);

	return ret < 0 ? ret : es7210_set_mute(dev, data->muted);
}

static int es7210_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	struct es7210_data *data = dev->data;

	if (cfg == NULL || cfg->dai_type != AUDIO_DAI_TYPE_I2S ||
	    cfg->dai_route != AUDIO_ROUTE_CAPTURE) {
		return -ENOTSUP;
	}
	if (cfg->dai_cfg.i2s.channels != 2 || cfg->dai_cfg.i2s.format != I2S_FMT_DATA_FORMAT_I2S) {
		return -ENOTSUP;
	}
	uint8_t serial_format;

	switch (cfg->dai_cfg.i2s.word_size) {
	case 16:
		serial_format = 0x60;
		break;
	case 24:
		serial_format = 0x00;
		break;
	case 32:
		serial_format = 0x80;
		break;
	default:
		return -ENOTSUP;
	}

	const struct {
		uint8_t reg;
		uint8_t value;
	} init_sequence[] = {
		{ES7210_RESET_REG00, 0xFF},
		{ES7210_RESET_REG00, 0x41},
		{ES7210_CLOCK_OFF_REG01, 0x3F},
		{ES7210_MAINCLK_REG02, 0xC1},
		{ES7210_TIME_CONTROL0_REG09, 0x30},
		{ES7210_TIME_CONTROL1_REG0A, 0x30},
		{ES7210_ADC12_HPF2_REG23, 0x2A},
		{ES7210_ADC12_HPF1_REG22, 0x0A},
		{ES7210_ADC34_HPF2_REG20, 0x0A},
		{ES7210_ADC34_HPF1_REG21, 0x2A},
		{ES7210_MODE_CONFIG_REG08, 0x00},
		{ES7210_MASTER_CLK_REG03, 0x00},
		{ES7210_ANALOG_REG40, 0x43},
		{ES7210_MIC12_BIAS_REG41, 0x70},
		{ES7210_MIC34_BIAS_REG42, 0x70},
		{ES7210_SDP_INTERFACE1_REG11, serial_format},
		{ES7210_SDP_INTERFACE2_REG12, 0x00},
	};

	for (size_t i = 0; i < ARRAY_SIZE(init_sequence); i++) {
		int ret = es7210_write_reg(dev, init_sequence[i].reg, init_sequence[i].value);

		if (ret < 0) {
			return ret;
		}
	}

	int ret = es7210_set_sample_rate(dev, cfg->mclk_freq, cfg->dai_cfg.i2s.frame_clk_freq);

	if (ret < 0) {
		return ret;
	}

	ret = es7210_apply_properties(dev);
	if (ret == 0) {
		ret = es7210_power_down(dev);
	}
	if (ret == 0) {
		data->configured = true;
		data->started = false;
	}
	return ret;
}

static int es7210_start(const struct device *dev, audio_dai_dir_t dir)
{
	const struct es7210_config *config = dev->config;
	struct es7210_data *data = dev->data;

	if ((dir & AUDIO_DAI_DIR_RX) == 0) {
		return 0;
	}
	if (!data->configured) {
		return -EIO;
	}
	if (data->started) {
		return 0;
	}

	const struct {
		uint8_t reg;
		uint8_t value;
	} start_sequence[] = {
		{ES7210_POWER_DOWN_REG06, 0x00},
		{ES7210_ANALOG_REG40, 0x43},
		{ES7210_MIC1_POWER_REG47, (config->input_channel_mask & ES7210_MIC1) ? 0x08 : 0xFF},
		{ES7210_MIC2_POWER_REG48, (config->input_channel_mask & ES7210_MIC2) ? 0x08 : 0xFF},
		{ES7210_MIC3_POWER_REG49, 0xFF},
		{ES7210_MIC4_POWER_REG4A, 0xFF},
	};

	int ret = es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, ES7210_CLOCK_ON_VALUE);

	for (size_t i = 0; i < ARRAY_SIZE(start_sequence) && ret == 0; i++) {
		ret = es7210_write_reg(dev, start_sequence[i].reg, start_sequence[i].value);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0x00);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0xFF);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_ANALOG_REG40, 0x43);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_RESET_REG00, 0x71);
	}
	if (ret == 0) {
		ret = es7210_write_reg(dev, ES7210_RESET_REG00, 0x41);
	}
	if (ret == 0) {
		ret = es7210_apply_properties(dev);
	}
	if (ret == 0) {
		data->started = true;
	}
	return ret;
}

static int es7210_stop(const struct device *dev, audio_dai_dir_t dir)
{
	struct es7210_data *data = dev->data;

	if ((dir & AUDIO_DAI_DIR_RX) == 0 || !data->started) {
		return 0;
	}
	int ret = es7210_power_down(dev);

	if (ret == 0) {
		data->started = false;
	}
	return ret;
}

static int es7210_set_property(const struct device *dev, audio_property_t property,
			       audio_channel_t channel, audio_property_value_t value)
{
	if (channel != AUDIO_CHANNEL_ALL) {
		return -ENOTSUP;
	}
	switch (property) {
	case AUDIO_PROPERTY_INPUT_VOLUME:
		value.vol = CLAMP(value.vol, 0, 100);
		return es7210_set_gain(dev, (uint8_t)(value.vol * ES7210_GAIN_MAX / 100));
	case AUDIO_PROPERTY_INPUT_MUTE:
		return es7210_set_mute(dev, value.mute);
	default:
		return -ENOTSUP;
	}
}

static DEVICE_API(audio_codec, es7210_api) = {
	.configure = es7210_configure,
	.set_property = es7210_set_property,
	.apply_properties = es7210_apply_properties,
	.start = es7210_start,
	.stop = es7210_stop,
};

static int es7210_init(const struct device *dev)
{
	const struct es7210_config *config = dev->config;
	struct es7210_data *data = dev->data;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus is not ready");
		return -ENODEV;
	}
	data->gain = config->mic_gain;
	return 0;
}

#define ES7210_INIT(inst)                                                                          \
	static struct es7210_data es7210_data_##inst;                                              \
	static const struct es7210_config es7210_config_##inst = {                                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.input_channel_mask = DT_INST_PROP(inst, input_channel_mask),                      \
		.mic_gain = DT_INST_PROP(inst, mic_gain),                                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, es7210_init, NULL, &es7210_data_##inst, &es7210_config_##inst, \
			      POST_KERNEL, CONFIG_AUDIO_ES7210_INIT_PRIORITY, &es7210_api);

DT_INST_FOREACH_STATUS_OKAY(ES7210_INIT)
