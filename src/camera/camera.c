/* SPDX-License-Identifier: Apache-2.0 */

#include "camera.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "../bus.h"

LOG_MODULE_REGISTER(app_camera, CONFIG_VIDEO_LOG_LEVEL);

#define CAMERA_BUFFER_COUNT 4

static const struct device *const camera = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
static struct video_buffer *camera_buffers[CAMERA_BUFFER_COUNT];
static bool camera_streaming;

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

int app_camera_get_caps(struct video_caps *caps)
{
	if (caps == NULL) {
		return -EINVAL;
	}
	if (!device_is_ready(camera)) {
		return -ENODEV;
	}

	*caps = (struct video_caps){.type = VIDEO_BUF_TYPE_OUTPUT};
	return video_get_caps(camera, caps);
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

	if (format == NULL) {
		return -EINVAL;
	}
	if (camera_streaming) {
		return -EALREADY;
	}
	app_camera_publish(APP_CAMERA_STARTING);
	if (!device_is_ready(camera)) {
		LOG_ERR("Camera capture device is not ready");
		app_camera_publish(APP_CAMERA_ERROR);
		return -ENODEV;
	}

	ret = video_get_caps(camera, &caps);
	if (ret < 0) {
		app_camera_publish(APP_CAMERA_ERROR);
		return ret;
	}
	if (caps.min_vbuf_count > CAMERA_BUFFER_COUNT) {
		app_camera_publish(APP_CAMERA_ERROR);
		return -ENOMEM;
	}

	ret = video_set_format(camera, format);
	if (ret < 0) {
		LOG_ERR("Unable to set camera format: %d", ret);
		app_camera_publish(APP_CAMERA_ERROR);
		return ret;
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
			app_camera_publish(APP_CAMERA_ERROR);
			return ret;
		}
		goto release_buffers;
	}

	camera_streaming = true;
	app_camera_publish(APP_CAMERA_ACTIVE);
	return 0;

release_buffers:
	video_driver_flush(camera, true);
	app_camera_release_buffers();
	app_camera_publish(APP_CAMERA_ERROR);
	return ret;
}

int app_camera_frame_get(struct video_buffer **buffer, k_timeout_t timeout)
{
	if (buffer == NULL) {
		return -EINVAL;
	}
	if (!camera_streaming) {
		return -EACCES;
	}

	return video_dequeue(camera, buffer, timeout);
}

int app_camera_frame_release(struct video_buffer *buffer)
{
	if (!camera_streaming || buffer == NULL) {
		return -EINVAL;
	}

	return video_enqueue(camera, buffer);
}

int app_camera_stop(void)
{
	int ret;

	if (!camera_streaming) {
		return 0;
	}

	ret = video_stream_stop(camera, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		app_camera_publish(APP_CAMERA_ERROR);
		return ret;
	}
	camera_streaming = false;

	ret = video_driver_flush(camera, true);
	if (ret < 0) {
		app_camera_publish(APP_CAMERA_ERROR);
		return ret;
	}
	app_camera_release_buffers();
	app_camera_publish(APP_CAMERA_READY);
	return 0;
}
