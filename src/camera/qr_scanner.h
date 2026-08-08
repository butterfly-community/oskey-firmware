/* SPDX-License-Identifier: Apache-2.0 */

#ifndef OSKEY_QR_SCANNER_H_
#define OSKEY_QR_SCANNER_H_

#include "qr_decoder.h"

#include <stddef.h>
#include <stdint.h>
#include <zephyr/video/video.h>
#include <zephyr/zbus/zbus.h>

enum app_qr_scanner_state {
	APP_QR_SCANNER_STOPPED,
	APP_QR_SCANNER_STARTING,
	APP_QR_SCANNER_RUNNING,
	APP_QR_SCANNER_STOPPING,
	APP_QR_SCANNER_RESULT,
	APP_QR_SCANNER_ERROR,
};

struct app_qr_scanner_event {
	enum app_qr_scanner_state state;
	uint32_t session;
	int error;
};

ZBUS_CHAN_DECLARE(app_qr_scanner_event_chan);

int app_qr_scanner_start(uint32_t *session);
int app_qr_scanner_stop(void);
int app_qr_scanner_get_format(struct video_format *format);
int app_qr_scanner_frame_copy(void *destination, size_t size, uint32_t *generation);
int app_qr_scanner_result_copy(uint32_t session, struct app_qr_code *code);

#endif /* OSKEY_QR_SCANNER_H_ */
