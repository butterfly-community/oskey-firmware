/* SPDX-License-Identifier: Apache-2.0 */

#ifndef OSKEY_CAMERA_CAMERA_H_
#define OSKEY_CAMERA_CAMERA_H_

#include <zephyr/kernel.h>
#include <zephyr/video/video.h>

int app_camera_start(struct video_format *format);
int app_camera_get_caps(struct video_caps *caps);
int app_camera_frame_get(struct video_buffer **buffer, k_timeout_t timeout);
int app_camera_frame_release(struct video_buffer *buffer);
int app_camera_stop(void);
int app_camera_init(void);

#endif /* OSKEY_CAMERA_CAMERA_H_ */
