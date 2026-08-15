/* SPDX-License-Identifier: MPL-2.0 */

#ifndef OSKEY_QR_DECODER_H_
#define OSKEY_QR_DECODER_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/video/video.h>

struct app_qr_code {
	uint8_t payload[CONFIG_OSKEY_QR_MAX_PAYLOAD_SIZE];
	size_t payload_len;
	uint32_t decode_time_ms;
	uint8_t version;
	uint8_t ecc_level;
};

typedef void (*app_qr_code_callback_t)(const struct app_qr_code *code, uint32_t session,
				       void *user_data);

int app_qr_decoder_init(const struct video_format *format, app_qr_code_callback_t callback,
			void *user_data);
int app_qr_decoder_submit(const struct video_buffer *buffer, const struct video_format *format,
			  uint32_t session);

#endif /* OSKEY_QR_DECODER_H_ */
