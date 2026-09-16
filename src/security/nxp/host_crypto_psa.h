/* SPDX-License-Identifier: MPL-2.0 */
#ifndef OSKEY_NXP_HOST_CRYPTO_PSA_H
#define OSKEY_NXP_HOST_CRYPTO_PSA_H

#include <stddef.h>
#include <stdint.h>

void nxp_wipe(void *buffer, size_t length);
/* Call under the same serialization as the nano-package session. */
void nxp_crypto_reset(void);
int nxp_pin_credential(const uint8_t pin_key[32], const uint8_t salt[16], uint8_t credential[16]);

#endif
