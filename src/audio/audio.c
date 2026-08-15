/* SPDX-License-Identifier: MPL-2.0 */

#include "audio.h"

#include <errno.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "bus.h"
#if defined(CONFIG_OSKEY_MICROPHONE)
#include "microphone.h"
#endif

LOG_MODULE_REGISTER(app_audio);

#define AUDIO_I2S_NODE   DT_ALIAS(audio_i2s)
#define AUDIO_CODEC_NODE DT_ALIAS(audio_codec)

#define AUDIO_CHANNELS    2U
#define AUDIO_SAMPLE_BITS 16U
#define AUDIO_SAMPLE_RATE 48000U
#define AUDIO_BLOCK_SIZE  1024U
#define AUDIO_BLOCK_COUNT 16

/* Embedded stereo 16-bit PCM beep (see assets/beep.wav). */
static const uint8_t beep_wav[] = {
#include "assets/generated/beep.wav.inc"
};
static const size_t beep_wav_size = sizeof(beep_wav);

K_MEM_SLAB_DEFINE(audio_i2s_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

struct wav_info {
	uint32_t sample_rate;
	uint16_t channels;
	uint16_t bits_per_sample;
	const uint8_t *data;
	size_t data_size;
};

static enum app_audio_state audio_state = APP_AUDIO_IDLE;
static uint8_t audio_volume = CONFIG_OSKEY_AUDIO_VOLUME;
static bool codec_ready;
static const struct device *audio_codec_dev;
static const struct device *audio_i2s_dev;
static uint8_t pending_volume;
#if defined(CONFIG_OSKEY_MICROPHONE)
static atomic_t pending_microphone_enabled;
#endif
static bool microphone_enabled;

static void audio_publish_state(enum app_audio_state state)
{
	struct app_audio_status status = {
		.state = state,
		.volume = audio_volume,
		.microphone_enabled = microphone_enabled,
	};

	audio_state = state;
	(void)zbus_chan_pub(&app_audio_state_chan, &status, K_MSEC(100));
}

static uint16_t wav_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t wav_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

/* Only the embedded stereo 16-bit PCM asset is supported. */
static int wav_parse(const uint8_t *wav, size_t size, struct wav_info *info)
{
	if (wav == NULL || info == NULL || size < 44) {
		return -EINVAL;
	}
	if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
		return -EINVAL;
	}

	bool have_fmt = false;
	bool have_data = false;
	size_t pos = 12;

	while (pos + 8 <= size) {
		uint32_t chunk_size = wav_le32(wav + pos + 4);

		if (memcmp(wav + pos, "fmt ", 4) == 0) {
			if (chunk_size < 16 || pos + 8 + chunk_size > size) {
				return -EINVAL;
			}
			info->sample_rate = wav_le32(wav + pos + 12);
			info->channels = wav_le16(wav + pos + 10);
			info->bits_per_sample = wav_le16(wav + pos + 22);
			have_fmt = true;
		} else if (memcmp(wav + pos, "data", 4) == 0) {
			size_t data_size = chunk_size;

			if (pos + 8 + data_size > size) {
				data_size = size - (pos + 8);
			}
			info->data = wav + pos + 8;
			info->data_size = data_size;
			have_data = true;
		}

		pos += 8 + chunk_size + (chunk_size & 1);
	}

	if (!have_fmt || !have_data || info->channels != 2 || info->bits_per_sample != 16) {
		return -ENOTSUP;
	}
	return 0;
}

/* Queue one block of PCM data; ownership transfers to the driver on success. */
static int audio_i2s_write(const struct device *i2s_dev, const uint8_t *data, size_t len)
{
	void *block = NULL;

	if (k_mem_slab_alloc(&audio_i2s_slab, &block, K_FOREVER) != 0) {
		return -ENOMEM;
	}
	memcpy(block, data, len);

	int ret = i2s_write(i2s_dev, block, len);

	if (ret < 0) {
		k_mem_slab_free(&audio_i2s_slab, block);
	}
	return ret;
}

static int audio_beep(void)
{
	if (!codec_ready) {
		LOG_ERR("Codec is not initialized");
		return -EIO;
	}
	if (!device_is_ready(audio_i2s_dev)) {
		LOG_ERR("I2S device not ready");
		return -ENODEV;
	}

	struct wav_info wav = {0};
	int ret = wav_parse(beep_wav, beep_wav_size, &wav);

	if (ret < 0) {
		LOG_ERR("Invalid beep WAV: %d", ret);
		return ret;
	}

	struct i2s_config i2s_cfg = {
		.frame_clk_freq = wav.sample_rate,
		.word_size = wav.bits_per_sample,
		.channels = wav.channels,
		.block_size = AUDIO_BLOCK_SIZE,
		.mem_slab = &audio_i2s_slab,
		.timeout = 1000,
		.options = 0,
		.format = I2S_FMT_DATA_FORMAT_I2S,
	};

	ret = i2s_configure(audio_i2s_dev, I2S_DIR_TX, &i2s_cfg);
	if (ret < 0) {
		LOG_ERR("I2S configure failed: %d", ret);
		return ret;
	}

	/* Power up the DAC and enable the amplifier before the clocks start. */
	ret = audio_codec_start(audio_codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		LOG_ERR("Codec start failed: %d", ret);
		return ret;
	}

	/*
	 * The ESP32 I2S driver dequeues the first TX block when START is
	 * triggered and flags an empty queue as an error, so prefill the TX
	 * queue first and keep feeding it while the DMA drains.
	 */
	const uint8_t *data = wav.data;
	size_t remaining = wav.data_size;
	size_t prefill = MAX(1, MIN(remaining / AUDIO_BLOCK_SIZE, CONFIG_I2S_ESP32_TX_BLOCK_COUNT));

	for (size_t i = 0; i < prefill; i++) {
		ret = audio_i2s_write(audio_i2s_dev, data, AUDIO_BLOCK_SIZE);
		if (ret < 0) {
			LOG_ERR("I2S write failed: %d", ret);
			goto out;
		}
		data += AUDIO_BLOCK_SIZE;
		remaining -= AUDIO_BLOCK_SIZE;
	}

	ret = i2s_trigger(audio_i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("I2S start failed: %d", ret);
		goto out;
	}

	while (remaining > 0) {
		size_t chunk = MIN(remaining, AUDIO_BLOCK_SIZE);

		ret = audio_i2s_write(audio_i2s_dev, data, chunk);
		if (ret < 0) {
			LOG_ERR("I2S write failed: %d", ret);
			break;
		}
		data += chunk;
		remaining -= chunk;
	}

	if (ret == 0) {
		/* Let the queued blocks finish before killing the output. */
		ret = i2s_trigger(audio_i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
		if (ret == 0) {
			uint32_t duration_ms =
				wav.data_size * 1000U /
				(wav.sample_rate * wav.channels * (wav.bits_per_sample / 8U));

			k_sleep(K_MSEC(duration_ms + 100));
		}
	}

out:
	(void)audio_codec_stop(audio_codec_dev, AUDIO_DAI_DIR_TX);
	i2s_trigger(audio_i2s_dev, I2S_DIR_TX, I2S_TRIGGER_STOP);
	if (ret == 0) {
		LOG_INF("Beep played");
	}
	return ret;
}

static void audio_set_volume(uint8_t volume)
{
	if (volume > 100) {
		volume = 100;
	}
	audio_volume = volume;

	audio_property_value_t value = {.vol = audio_volume};

	if (codec_ready && audio_codec_set_property(audio_codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
						    AUDIO_CHANNEL_ALL, value) < 0) {
		LOG_ERR("Volume update failed");
	}
	audio_publish_state(audio_state);
}

static void audio_beep_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

#if defined(CONFIG_OSKEY_MICROPHONE)
	int ret = app_microphone_pause();
	if (ret < 0) {
		LOG_WRN("Beep skipped because microphone capture did not pause: %d", ret);
		return;
	}
#endif
	audio_publish_state(APP_AUDIO_PLAYING);
	(void)audio_beep();
	audio_publish_state(APP_AUDIO_IDLE);
#if defined(CONFIG_OSKEY_MICROPHONE)
	app_microphone_resume();
#endif
}

K_WORK_DEFINE(audio_beep_work, audio_beep_work_handler);

static void audio_volume_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	audio_set_volume(pending_volume);
}

K_WORK_DEFINE(audio_volume_work, audio_volume_work_handler);

#if defined(CONFIG_OSKEY_MICROPHONE)
static void audio_microphone_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	microphone_enabled = atomic_get(&pending_microphone_enabled);
	app_microphone_set_enabled(microphone_enabled);
	audio_publish_state(audio_state);
}

K_WORK_DEFINE(audio_microphone_work, audio_microphone_work_handler);
#endif

static void audio_command_listener(const struct zbus_channel *chan)
{
	const struct app_audio_command *command = zbus_chan_const_msg(chan);

	switch (command->kind) {
	case APP_AUDIO_COMMAND_BEEP:
		k_work_submit(&audio_beep_work);
		break;
	case APP_AUDIO_COMMAND_SET_VOLUME:
		pending_volume = command->volume;
		k_work_submit(&audio_volume_work);
		break;
	case APP_AUDIO_COMMAND_SET_MICROPHONE:
#if defined(CONFIG_OSKEY_MICROPHONE)
		atomic_set(&pending_microphone_enabled, command->enabled);
		k_work_submit(&audio_microphone_work);
#endif
		break;
	default:
		break;
	}
}

static void audio_notification_listener(const struct zbus_channel *chan)
{
	const struct app_notification *notification = zbus_chan_const_msg(chan);

	if (notification->kind != APP_NOTIFICATION_NONE) {
		k_work_submit(&audio_beep_work);
	}
}

ZBUS_LISTENER_DEFINE(audio_command_listener_ob, audio_command_listener);
ZBUS_CHAN_ADD_OBS(app_audio_command_chan, audio_command_listener_ob, 0);

ZBUS_LISTENER_DEFINE(audio_notification_listener_ob, audio_notification_listener);
ZBUS_CHAN_ADD_OBS(app_notification_event_chan, audio_notification_listener_ob, 0);

int app_audio_init(void)
{
	audio_i2s_dev = DEVICE_DT_GET(AUDIO_I2S_NODE);
	audio_codec_dev = DEVICE_DT_GET(AUDIO_CODEC_NODE);
	if (!device_is_ready(audio_i2s_dev)) {
		LOG_ERR("Audio I2S device is not ready");
		return -ENODEV;
	}
	if (!device_is_ready(audio_codec_dev)) {
		LOG_ERR("Audio codec is not ready");
		return -ENODEV;
	}

	struct audio_codec_cfg cfg = {
		.mclk_freq = AUDIO_SAMPLE_RATE * 256U,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_cfg =
			{
				.i2s =
					{
						.frame_clk_freq = AUDIO_SAMPLE_RATE,
						.word_size = AUDIO_SAMPLE_BITS,
						.channels = AUDIO_CHANNELS,
						.format = I2S_FMT_DATA_FORMAT_I2S,
					},
			},
		.dai_route = AUDIO_ROUTE_PLAYBACK,
	};

	int ret = audio_codec_configure(audio_codec_dev, &cfg);
	if (ret == 0) {
		audio_property_value_t value = {.vol = audio_volume};

		ret = audio_codec_set_property(audio_codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					       AUDIO_CHANNEL_ALL, value);
	}
	if (ret < 0) {
		LOG_ERR("Codec init failed: %d", ret);
		return ret;
	}

	codec_ready = true;

#if defined(CONFIG_OSKEY_MICROPHONE)
	ret = app_microphone_init();
	if (ret < 0) {
		LOG_ERR("Microphone init failed: %d", ret);
		return ret;
	}
#endif
	audio_publish_state(APP_AUDIO_IDLE);
	return 0;
}
