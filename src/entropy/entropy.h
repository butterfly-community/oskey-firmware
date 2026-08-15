/* SPDX-License-Identifier: MPL-2.0 */

#ifndef OSKEY_ENTROPY_H
#define OSKEY_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#define APP_ENTROPY_TRANSCRIPT_MAX_SIZE 174U

enum app_entropy_source {
	APP_ENTROPY_SOURCE_NONE = 0,
	APP_ENTROPY_SOURCE_HARDWARE_RNG = 1U << 0,
	APP_ENTROPY_SOURCE_TOUCH = 1U << 1,
	APP_ENTROPY_SOURCE_IMU = 1U << 2,
	APP_ENTROPY_SOURCE_CAMERA = 1U << 3,
	APP_ENTROPY_SOURCE_MICROPHONE = 1U << 4,
};

#define APP_ENTROPY_SOURCE_AUXILIARY_MASK                                                          \
	(APP_ENTROPY_SOURCE_TOUCH | APP_ENTROPY_SOURCE_IMU | APP_ENTROPY_SOURCE_CAMERA |           \
	 APP_ENTROPY_SOURCE_MICROPHONE)

enum app_entropy_state {
	APP_ENTROPY_IDLE,
	APP_ENTROPY_CAPTURING,
	APP_ENTROPY_READY,
	APP_ENTROPY_ERROR,
};

struct app_entropy_snapshot {
	uint32_t session;
	uint16_t progress_permille;
	uint8_t completed;
	uint8_t skipped;
	uint8_t current;
	enum app_entropy_state state;
};

uint8_t app_entropy_capabilities(void);
int app_entropy_begin(uint8_t words, uint8_t sources, uint32_t *session);
int app_entropy_feed(uint32_t session, enum app_entropy_source source, const void *data, size_t len,
		     uint32_t units);
int app_entropy_fail(uint32_t session, enum app_entropy_source source, int error);
int app_entropy_retry(uint32_t session);
int app_entropy_skip(uint32_t session);
int app_entropy_snapshot_get(struct app_entropy_snapshot *snapshot);
int app_entropy_transcript_get(uint32_t session, void *buffer, size_t size, size_t *written);
void app_entropy_cancel(uint32_t session);

#endif /* OSKEY_ENTROPY_H */
