/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSKEY_USB_FIDO2_H
#define OSKEY_USB_FIDO2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void fido2_message_reply(bool success, const uint8_t *credential_id, size_t credential_id_len,
			 const uint8_t *data, size_t data_len);

#endif /* OSKEY_USB_FIDO2_H */
