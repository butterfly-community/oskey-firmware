/* SPDX-License-Identifier: Apache-2.0 */

#include "microphone.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/usbd_uac2.h>

LOG_MODULE_REGISTER(app_microphone);

#define MICROPHONE_I2S_NODE        DT_ALIAS(audio_i2s)
#define MICROPHONE_CODEC_NODE      DT_ALIAS(audio_codec_input)
#define USB_MICROPHONE_NODE        DT_NODELABEL(usb_microphone)
#define USB_MICROPHONE_TERMINAL_ID UAC2_ENTITY_ID(DT_NODELABEL(usb_mic_output))
#define USB_MICROPHONE_CLOCK_NODE  DT_NODELABEL(usb_mic_clock)
#define USB_MICROPHONE_CLOCK_ID    UAC2_ENTITY_ID(USB_MICROPHONE_CLOCK_NODE)

#define MICROPHONE_DEFAULT_SAMPLE_RATE                                                             \
	DT_PROP_BY_IDX(USB_MICROPHONE_CLOCK_NODE, sampling_frequencies, 0)
#define MICROPHONE_MAX_SAMPLE_RATE                                                                 \
	DT_PROP_LAST(USB_MICROPHONE_CLOCK_NODE, sampling_frequencies)
#define MICROPHONE_CHANNELS          2U
#define MICROPHONE_SAMPLE_BITS       16U
#define MICROPHONE_BYTES_PER_FRAME   (MICROPHONE_CHANNELS * sizeof(int16_t))
#define MICROPHONE_BLOCK_DURATION_MS 10U
#define MICROPHONE_BLOCK_COUNT       8U
#define MICROPHONE_MAX_BLOCK_SIZE                                                                  \
	(MICROPHONE_MAX_SAMPLE_RATE * MICROPHONE_BLOCK_DURATION_MS / 1000U *                       \
	 MICROPHONE_BYTES_PER_FRAME)
#define MICROPHONE_RING_SIZE (MICROPHONE_MAX_BLOCK_SIZE * MICROPHONE_BLOCK_COUNT)
#define USB_MAX_PACKET_SIZE  ((MICROPHONE_MAX_SAMPLE_RATE / 1000U + 1U) * MICROPHONE_BYTES_PER_FRAME)
#define USB_PACKET_COUNT     8U

BUILD_ASSERT(DT_PROP_LEN(USB_MICROPHONE_CLOCK_NODE, sampling_frequencies) > 0,
	     "USB microphone requires at least one sample rate");
BUILD_ASSERT(MICROPHONE_DEFAULT_SAMPLE_RATE <= MICROPHONE_MAX_SAMPLE_RATE);
BUILD_ASSERT(MICROPHONE_DEFAULT_SAMPLE_RATE % 1000U == 0U);
BUILD_ASSERT(MICROPHONE_MAX_SAMPLE_RATE % 1000U == 0U);
BUILD_ASSERT(DT_NODE_HAS_STATUS(MICROPHONE_CODEC_NODE, okay),
	     "audio-codec-input alias must reference an enabled input codec");

K_MEM_SLAB_DEFINE_STATIC(microphone_i2s_slab, MICROPHONE_MAX_BLOCK_SIZE, MICROPHONE_BLOCK_COUNT, 4);
K_MEM_SLAB_DEFINE_STATIC(microphone_usb_slab, ROUND_UP(USB_MAX_PACKET_SIZE, UDC_BUF_GRANULARITY),
			 USB_PACKET_COUNT, UDC_BUF_ALIGN);
RING_BUF_DECLARE(microphone_pcm, MICROPHONE_RING_SIZE);
K_SEM_DEFINE(microphone_state_changed, 0, 1);
K_SEM_DEFINE(microphone_stopped, 0, 1);

static const struct device *microphone_i2s_dev;
static const struct device *microphone_codec_dev;
static const uint32_t microphone_sample_rates[] =
	DT_PROP(USB_MICROPHONE_CLOCK_NODE, sampling_frequencies);
static atomic_t microphone_enabled = ATOMIC_INIT(0);
static atomic_t microphone_usb_active;
static atomic_t microphone_paused;
static atomic_t microphone_running;
static atomic_t microphone_sample_rate = ATOMIC_INIT(MICROPHONE_DEFAULT_SAMPLE_RATE);
static atomic_t microphone_active_sample_rate;
static struct k_spinlock microphone_ring_lock;
static bool microphone_stream_ready;

static bool microphone_sample_rate_supported(uint32_t sample_rate)
{
	for (size_t i = 0; i < ARRAY_SIZE(microphone_sample_rates); i++) {
		if (microphone_sample_rates[i] == sample_rate) {
			return true;
		}
	}
	return false;
}

static size_t microphone_block_size(uint32_t sample_rate)
{
	return sample_rate * MICROPHONE_BLOCK_DURATION_MS / 1000U * MICROPHONE_BYTES_PER_FRAME;
}

static size_t microphone_usb_packet_size(uint32_t sample_rate, int frame_adjustment)
{
	int frames = (int)(sample_rate / 1000U) + frame_adjustment;

	return (size_t)frames * MICROPHONE_BYTES_PER_FRAME;
}

static bool microphone_should_run(void)
{
	return atomic_get(&microphone_enabled) && atomic_get(&microphone_usb_active) &&
	       !atomic_get(&microphone_paused);
}

static void microphone_ring_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&microphone_ring_lock);

	ring_buf_reset(&microphone_pcm);
	microphone_stream_ready = false;
	k_spin_unlock(&microphone_ring_lock, key);
}

static void microphone_ring_write(const uint8_t *data, size_t size)
{
	k_spinlock_key_t key = k_spin_lock(&microphone_ring_lock);
	uint32_t space = ring_buf_space_get(&microphone_pcm);

	if (space < size) {
		(void)ring_buf_get(&microphone_pcm, NULL, size - space);
	}
	(void)ring_buf_put(&microphone_pcm, data, size);
	k_spin_unlock(&microphone_ring_lock, key);
}

static size_t microphone_ring_read(uint8_t *data, uint32_t sample_rate)
{
	size_t block_size = microphone_block_size(sample_rate);
	size_t packet_size = microphone_usb_packet_size(sample_rate, 0);
	k_spinlock_key_t key = k_spin_lock(&microphone_ring_lock);
	uint32_t available = ring_buf_size_get(&microphone_pcm);

	if (!microphone_stream_ready && available >= block_size * 2U) {
		microphone_stream_ready = true;
	}
	if (!microphone_stream_ready) {
		packet_size = 0;
	} else {
		if (available > block_size * 5U) {
			packet_size = microphone_usb_packet_size(sample_rate, 1);
		} else if (available < block_size) {
			packet_size = microphone_usb_packet_size(sample_rate, -1);
		}

		if (available < packet_size) {
			microphone_stream_ready = false;
			packet_size = 0;
		} else {
			packet_size = ring_buf_get(&microphone_pcm, data, packet_size);
		}
	}
	k_spin_unlock(&microphone_ring_lock, key);

	return packet_size;
}

static int microphone_capture_start(uint32_t sample_rate)
{
	size_t block_size = microphone_block_size(sample_rate);
	struct i2s_config i2s_cfg = {
		.word_size = MICROPHONE_SAMPLE_BITS,
		.channels = MICROPHONE_CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = 0,
		.frame_clk_freq = sample_rate,
		.block_size = block_size,
		.mem_slab = &microphone_i2s_slab,
		.timeout = 100,
	};
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = sample_rate * 256U,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_cfg = {.i2s = {.frame_clk_freq = sample_rate,
				    .word_size = MICROPHONE_SAMPLE_BITS,
				    .channels = MICROPHONE_CHANNELS,
				    .format = I2S_FMT_DATA_FORMAT_I2S}},
		.dai_route = AUDIO_ROUTE_CAPTURE,
	};

	microphone_ring_reset();
	int ret = i2s_configure(microphone_i2s_dev, I2S_DIR_RX, &i2s_cfg);

	if (ret == 0) {
		ret = audio_codec_configure(microphone_codec_dev, &codec_cfg);
	}
	if (ret == 0) {
		ret = audio_codec_start(microphone_codec_dev, AUDIO_DAI_DIR_RX);
	}
	if (ret == 0) {
		ret = i2s_trigger(microphone_i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
	}
	if (ret < 0) {
		(void)audio_codec_stop(microphone_codec_dev, AUDIO_DAI_DIR_RX);
		(void)i2s_trigger(microphone_i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
		return ret;
	}

	atomic_set(&microphone_active_sample_rate, sample_rate);
	atomic_set(&microphone_running, 1);
	LOG_INF("USB microphone capture started at %u Hz", sample_rate);
	return 0;
}

static void microphone_capture_stop(void)
{
	atomic_clear(&microphone_running);
	atomic_clear(&microphone_active_sample_rate);
	(void)i2s_trigger(microphone_i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
	(void)audio_codec_stop(microphone_codec_dev, AUDIO_DAI_DIR_RX);
	microphone_ring_reset();
	k_sem_give(&microphone_stopped);
	LOG_INF("USB microphone capture stopped");
}

static void microphone_capture_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	bool running = false;
	uint32_t active_sample_rate = 0U;

	while (true) {
		if (!running) {
			k_sem_take(&microphone_state_changed, K_FOREVER);
			if (!microphone_should_run()) {
				continue;
			}
			active_sample_rate = atomic_get(&microphone_sample_rate);
			int ret = microphone_capture_start(active_sample_rate);

			if (ret < 0) {
				LOG_ERR("USB microphone start failed: %d", ret);
				k_sleep(K_MSEC(100));
				k_sem_give(&microphone_state_changed);
				continue;
			}
			running = true;
		}

		void *block;
		size_t size;
		int ret = i2s_read(microphone_i2s_dev, &block, &size);

		if (ret == 0) {
			if (size == microphone_block_size(active_sample_rate)) {
				microphone_ring_write(block, size);
			}
			k_mem_slab_free(&microphone_i2s_slab, block);
		}

		bool capture_requested = microphone_should_run();
		bool capture_failed = ret < 0 && ret != -EAGAIN;
		bool reconfigure = active_sample_rate != atomic_get(&microphone_sample_rate);

		if (!capture_requested || capture_failed || reconfigure) {
			if (capture_failed) {
				LOG_ERR("I2S microphone read failed: %d", ret);
			}
			microphone_capture_stop();
			running = false;
			if (capture_requested) {
				if (capture_failed) {
					k_sleep(K_MSEC(100));
				}
				k_sem_give(&microphone_state_changed);
			}
		}
	}
}

K_THREAD_DEFINE(microphone_thread_id, CONFIG_OSKEY_MICROPHONE_STACK_SIZE, microphone_capture_thread,
		NULL, NULL, NULL, K_PRIO_PREEMPT(4), 0, 0);

static void microphone_terminal_update(const struct device *dev, uint8_t terminal, bool enabled,
				       bool microframes, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(microframes);
	ARG_UNUSED(user_data);

	if (terminal == USB_MICROPHONE_TERMINAL_ID) {
		atomic_set(&microphone_usb_active, enabled);
		k_sem_give(&microphone_state_changed);
	}
}

static uint32_t microphone_get_sample_rate(const struct device *dev, uint8_t clock_id,
					   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	return clock_id == USB_MICROPHONE_CLOCK_ID ? atomic_get(&microphone_sample_rate) : 0U;
}

static int microphone_set_sample_rate(const struct device *dev, uint8_t clock_id,
				      uint32_t sample_rate, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (clock_id != USB_MICROPHONE_CLOCK_ID || !microphone_sample_rate_supported(sample_rate)) {
		return -EINVAL;
	}
	if (sample_rate == atomic_get(&microphone_sample_rate)) {
		return 0;
	}

	atomic_set(&microphone_sample_rate, sample_rate);
	microphone_ring_reset();
	k_sem_give(&microphone_state_changed);
	LOG_INF("USB microphone sample rate set to %u Hz", sample_rate);
	return 0;
}

static void microphone_usb_release(const struct device *dev, uint8_t terminal, void *buffer,
				   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (terminal == USB_MICROPHONE_TERMINAL_ID) {
		k_mem_slab_free(&microphone_usb_slab, buffer);
	}
}

static void microphone_sof(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!atomic_get(&microphone_usb_active)) {
		return;
	}

	void *buffer;
	if (k_mem_slab_alloc(&microphone_usb_slab, &buffer, K_NO_WAIT) < 0) {
		return;
	}

	size_t size = 0;
	uint32_t sample_rate = atomic_get(&microphone_sample_rate);
	if (atomic_get(&microphone_enabled) && atomic_get(&microphone_running) &&
	    atomic_get(&microphone_active_sample_rate) == sample_rate) {
		size = microphone_ring_read(buffer, sample_rate);
	}
	if (size == 0) {
		size = microphone_usb_packet_size(sample_rate, 0);
		memset(buffer, 0, size);
	}

	if (usbd_uac2_send(dev, USB_MICROPHONE_TERMINAL_ID, buffer, size) < 0) {
		k_mem_slab_free(&microphone_usb_slab, buffer);
	}
}

static const struct uac2_ops microphone_usb_ops = {
	.get_sample_rate = microphone_get_sample_rate,
	.set_sample_rate = microphone_set_sample_rate,
	.sof_cb = microphone_sof,
	.terminal_update_cb = microphone_terminal_update,
	.buf_release_cb = microphone_usb_release,
};

int app_microphone_init(void)
{
	microphone_i2s_dev = DEVICE_DT_GET(MICROPHONE_I2S_NODE);
	microphone_codec_dev = DEVICE_DT_GET(MICROPHONE_CODEC_NODE);
	const struct device *usb_dev = DEVICE_DT_GET(USB_MICROPHONE_NODE);

	if (!device_is_ready(microphone_i2s_dev) || !device_is_ready(microphone_codec_dev) ||
	    !device_is_ready(usb_dev)) {
		return -ENODEV;
	}

	usbd_uac2_set_ops(usb_dev, &microphone_usb_ops, NULL);
	return 0;
}

void app_microphone_set_enabled(bool enabled)
{
	atomic_set(&microphone_enabled, enabled);
	if (!enabled) {
		microphone_ring_reset();
	}
	k_sem_give(&microphone_state_changed);
}

int app_microphone_pause(void)
{
	atomic_set(&microphone_paused, 1);
	k_sem_reset(&microphone_stopped);
	if (!atomic_get(&microphone_running)) {
		return 0;
	}

	k_sem_give(&microphone_state_changed);
	return k_sem_take(&microphone_stopped, K_MSEC(250));
}

void app_microphone_resume(void)
{
	atomic_clear(&microphone_paused);
	k_sem_give(&microphone_state_changed);
}
