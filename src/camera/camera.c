/* SPDX-License-Identifier: Apache-2.0 */

#include "camera.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "../bus.h"

LOG_MODULE_REGISTER(app_camera, CONFIG_VIDEO_LOG_LEVEL);

#define CAMERA_BUFFER_COUNT 4

static const struct device *const camera = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
static struct video_buffer *camera_buffers[CAMERA_BUFFER_COUNT];
static atomic_t camera_in_use;

static void app_camera_publish(enum app_camera_state state)
{
	if (zbus_chan_pub(&app_camera_state_chan, &state, K_FOREVER) < 0) {
		LOG_WRN("Unable to publish camera state %d", state);
	}
}

int app_camera_init(void)
{
	if (!device_is_ready(camera)) {
		LOG_ERR("Camera capture device is not ready");
		app_camera_publish(APP_CAMERA_ERROR);
		return -ENODEV;
	}

	app_camera_publish(APP_CAMERA_READY);
	return 0;
}

static bool app_camera_dimension_supported(uint32_t value, uint32_t minimum, uint32_t maximum,
					   uint32_t step)
{
	if (value < minimum || value > maximum) {
		return false;
	}
	if (step == 0U) {
		return minimum == maximum;
	}
	return (value - minimum) % step == 0U;
}

int app_camera_select_rgb565_format(uint32_t width, uint32_t height, struct video_format *format)
{
	struct video_caps caps = {.type = VIDEO_BUF_TYPE_OUTPUT};

	if (format == NULL || width == 0U || height == 0U) {
		return -EINVAL;
	}
	if (!device_is_ready(camera)) {
		return -ENODEV;
	}

	int ret = video_get_caps(camera, &caps);
	if (ret < 0 || caps.format_caps == NULL) {
		return ret < 0 ? ret : -ENOTSUP;
	}

	for (const struct video_format_cap *cap = caps.format_caps; cap->pixelformat != 0U; cap++) {
		if ((cap->pixelformat != VIDEO_PIX_FMT_RGB565X &&
		     cap->pixelformat != VIDEO_PIX_FMT_RGB565) ||
		    !app_camera_dimension_supported(width, cap->width_min, cap->width_max,
						    cap->width_step) ||
		    !app_camera_dimension_supported(height, cap->height_min, cap->height_max,
						    cap->height_step)) {
			continue;
		}

		*format = (struct video_format){
			.type = VIDEO_BUF_TYPE_OUTPUT,
			.pixelformat = cap->pixelformat,
			.width = width,
			.height = height,
		};
		return 0;
	}
	return -ENOTSUP;
}

static void app_camera_release_buffers(void)
{
	struct video_buffer *buffer;

	while (video_dequeue(camera, &buffer, K_NO_WAIT) == 0) {
		for (size_t i = 0; i < ARRAY_SIZE(camera_buffers); i++) {
			if (camera_buffers[i] == buffer) {
				camera_buffers[i] = NULL;
				break;
			}
		}
		video_buffer_release(buffer);
	}

	/* Release buffers that were not returned through the driver's output queue. */
	for (size_t i = 0; i < ARRAY_SIZE(camera_buffers); i++) {
		if (camera_buffers[i] != NULL) {
			video_buffer_release(camera_buffers[i]);
			camera_buffers[i] = NULL;
		}
	}
}

int app_camera_start(struct video_format *format)
{
	struct video_caps caps = {.type = VIDEO_BUF_TYPE_OUTPUT};
	int ret;
	int flush_ret;

	if (format == NULL) {
		return -EINVAL;
	}
	if (!atomic_cas(&camera_in_use, 0, 1)) {
		return -EBUSY;
	}
	for (size_t i = 0; i < ARRAY_SIZE(camera_buffers); i++) {
		if (camera_buffers[i] != NULL) {
			LOG_ERR("Camera buffers are still owned by the driver");
			ret = -EBUSY;
			goto error;
		}
	}
	app_camera_publish(APP_CAMERA_STARTING);
	if (!device_is_ready(camera)) {
		LOG_ERR("Camera capture device is not ready");
		ret = -ENODEV;
		goto error;
	}

	ret = video_get_caps(camera, &caps);
	if (ret < 0) {
		goto error;
	}
	if (caps.min_vbuf_count > CAMERA_BUFFER_COUNT) {
		ret = -ENOMEM;
		goto error;
	}

	ret = video_set_format(camera, format);
	if (ret < 0) {
		LOG_ERR("Unable to set camera format: %d", ret);
		goto error;
	}

	for (size_t i = 0; i < ARRAY_SIZE(camera_buffers); i++) {
		camera_buffers[i] = video_buffer_aligned_alloc(
			format->size, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
		if (camera_buffers[i] == NULL) {
			LOG_ERR("Unable to allocate camera buffer %u", (unsigned int)i);
			ret = -ENOMEM;
			goto release_buffers;
		}

		camera_buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_enqueue(camera, camera_buffers[i]);
		if (ret < 0) {
			LOG_ERR("Unable to enqueue camera buffer %u: %d", (unsigned int)i, ret);
			goto release_buffers;
		}
	}

	ret = video_stream_start(camera, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Unable to start camera stream: %d", ret);
		int stop_ret = video_stream_stop(camera, VIDEO_BUF_TYPE_OUTPUT);

		if (stop_ret < 0) {
			LOG_ERR("Unable to stop camera after start failure: %d", stop_ret);
			goto error;
		}
		goto release_buffers;
	}

	app_camera_publish(APP_CAMERA_ACTIVE);
	return 0;

release_buffers:
	flush_ret = video_driver_flush(camera, true);
	if (flush_ret < 0) {
		LOG_ERR("Unable to flush camera buffers: %d", flush_ret);
	} else {
		app_camera_release_buffers();
	}
error:
	app_camera_publish(APP_CAMERA_ERROR);
	atomic_clear(&camera_in_use);
	return ret;
}

int app_camera_frame_get(struct video_buffer **buffer, k_timeout_t timeout)
{
	if (buffer == NULL) {
		return -EINVAL;
	}
	if (!atomic_get(&camera_in_use)) {
		return -EACCES;
	}

	return video_dequeue(camera, buffer, timeout);
}

int app_camera_frame_release(struct video_buffer *buffer)
{
	if (!atomic_get(&camera_in_use) || buffer == NULL) {
		return -EINVAL;
	}

	return video_enqueue(camera, buffer);
}

int app_camera_stop(void)
{
	int ret;

	if (!atomic_get(&camera_in_use)) {
		return 0;
	}

	ret = video_stream_stop(camera, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		app_camera_publish(APP_CAMERA_ERROR);
		return ret;
	}

	ret = video_driver_flush(camera, true);
	if (ret < 0) {
		LOG_ERR("Unable to flush camera buffers: %d", ret);
		app_camera_publish(APP_CAMERA_ERROR);
		return ret;
	}
	app_camera_release_buffers();
	atomic_clear(&camera_in_use);
	app_camera_publish(APP_CAMERA_READY);
	return 0;
}
