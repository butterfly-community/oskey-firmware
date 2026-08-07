/* SPDX-License-Identifier: Apache-2.0 */

#include "camera_test.h"

#include "camera.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(camera_test, LOG_LEVEL_INF);

static const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);

static uint32_t camera_frame_sample_hash(const struct video_buffer *buffer)
{
	uint32_t hash = 2166136261U;

	for (size_t i = 0; i < buffer->bytesused; i += 64U) {
		hash = (hash ^ buffer->buffer[i]) * 16777619U;
	}

	return hash;
}

int app_camera_test_run(void)
{
	struct video_format format;
	uint32_t frames = 0;
	uint32_t last_timestamp = 0;
	int stop_ret;
	int ret;

	if (!device_is_ready(display) || !gpio_is_ready_dt(&backlight)) {
		LOG_ERR("Display or backlight is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = display_blanking_off(display);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Unable to enable display: %d", ret);
		return ret;
	}

	ret = app_camera_start(&format);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("Camera preview started: %ux%u, %u-byte buffers", format.width, format.height,
		format.size);

	while (true) {
		struct video_buffer *buffer;
		struct display_buffer_descriptor descriptor;

		ret = app_camera_frame_get(&buffer, K_SECONDS(2));
		if (ret < 0) {
			LOG_ERR("Camera dequeue failed: %d", ret);
			break;
		}

		descriptor = (struct display_buffer_descriptor){
			.buf_size = buffer->bytesused,
			.width = format.width,
			.height = format.height,
			.pitch = format.width,
		};
		ret = display_write(display, 0, 0, &descriptor, buffer->buffer);
		if (ret < 0) {
			LOG_ERR("Display write failed: %d", ret);
			break;
		}

		frames++;
		if (frames <= 3U || (frames % 30U) == 0U) {
			LOG_INF("Frame %u: %u bytes, interval %u ms, sample %08x", frames,
				buffer->bytesused, buffer->timestamp - last_timestamp,
				camera_frame_sample_hash(buffer));
		}
		last_timestamp = buffer->timestamp;

		ret = app_camera_frame_release(buffer);
		if (ret < 0) {
			LOG_ERR("Camera requeue failed: %d", ret);
			break;
		}
	}

	stop_ret = app_camera_stop();
	if (stop_ret < 0) {
		LOG_ERR("Unable to stop camera stream: %d", stop_ret);
	}
	return ret;
}
