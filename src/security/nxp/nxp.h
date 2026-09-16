/* SPDX-License-Identifier: MPL-2.0 */
#ifndef OSKEY_NXP_H
#define OSKEY_NXP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum nxp_state {
	NXP_UNCONFIGURED,
	NXP_UNAVAILABLE,
	NXP_EMPTY,
	NXP_READY,
	NXP_AUTH_REJECTED,
	NXP_INCOMPLETE
};
/* A5000 does not expose a UserID retry counter. AUTH_REJECTED includes exhausted
 * retries; only a complete erase can recover an exhausted authentication object. */
enum nxp_result {
	NXP_OK = 0,
	NXP_ERROR = -1,
	NXP_PIN_REJECTED = -2,
	NXP_CREDENTIALS_MISSING = -3
};
bool app_nxp_enabled(void);
int app_nxp_refresh(void);
int app_nxp_state(void);
int app_nxp_seed_exists(void);
int app_nxp_initialize(const uint8_t pin_key[32], const uint8_t seed[64]);
int app_nxp_unlock(const uint8_t pin_key[32], uint8_t seed[64]);
int app_nxp_erase(void);
#endif
