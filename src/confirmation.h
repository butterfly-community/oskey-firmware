/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OSKEY_CONFIRMATION_H
#define OSKEY_CONFIRMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bindings.h"

int app_confirmation_wait(AppMessageSource source, int32_t timeout_ms);
void app_confirmation_cancel(AppMessageSource source);
void app_confirmation_complete(bool approved);
void app_confirmation_respond(bool approved);
void app_confirmation_prompt(ConfirmationKind kind, bool active, const uint8_t *data, size_t len);

#endif /* OSKEY_CONFIRMATION_H */
