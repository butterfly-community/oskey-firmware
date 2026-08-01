/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "confirmation.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "display/display.h"
#include "message.h"

static K_SEM_DEFINE(confirmation_done, 0, 1);
static atomic_t confirmation_active;

enum confirmation_state {
	CONFIRMATION_IDLE,
	CONFIRMATION_WAITING,
	CONFIRMATION_APPROVED,
	CONFIRMATION_REJECTED,
};

static atomic_t confirmation_state;

static void confirmation_control(AppMessageSource source, bool active)
{
	while (!app_message_submit(source, AppMessageAction_Confirmation, active, NULL, 0, NULL,
				   0)) {
		k_sleep(K_MSEC(1));
	}
}

int app_confirmation_wait(AppMessageSource source, int32_t timeout_ms, FidoOperation operation,
			  const uint8_t *data, size_t len, const uint8_t *auxiliary,
			  size_t auxiliary_len)
{
	if (!atomic_cas(&confirmation_state, CONFIRMATION_IDLE, CONFIRMATION_WAITING)) {
		return -EBUSY;
	}

	k_sem_reset(&confirmation_done);

	if (!app_message_submit(source, AppMessageAction_Confirmation, operation, data, len,
				auxiliary, auxiliary_len)) {
		atomic_set(&confirmation_state, CONFIRMATION_IDLE);
		return -EIO;
	}

	if (k_sem_take(&confirmation_done, K_MSEC(timeout_ms)) != 0) {
		if (atomic_cas(&confirmation_state, CONFIRMATION_WAITING, CONFIRMATION_IDLE)) {
			confirmation_control(source, false);
			return -ETIMEDOUT;
		}
		k_sem_take(&confirmation_done, K_FOREVER);
	}

	atomic_val_t result = atomic_set(&confirmation_state, CONFIRMATION_IDLE);
	return result == CONFIRMATION_APPROVED ? 0 : -ECANCELED;
}

void app_confirmation_cancel(AppMessageSource source)
{
	if (atomic_cas(&confirmation_state, CONFIRMATION_WAITING, CONFIRMATION_REJECTED)) {
		confirmation_control(source, false);
		k_sem_give(&confirmation_done);
	}
}

void app_confirmation_complete(bool approved)
{
	atomic_val_t result = approved ? CONFIRMATION_APPROVED : CONFIRMATION_REJECTED;
	if (atomic_cas(&confirmation_state, CONFIRMATION_WAITING, result)) {
		k_sem_give(&confirmation_done);
	}
}

void app_confirmation_respond(bool approved)
{
	if (!atomic_cas(&confirmation_active, true, false)) {
		return;
	}

	AppMessageAction action = approved ? AppMessageAction_Approve : AppMessageAction_Reject;
	if (!app_message_submit(AppMessageSource_Confirmation, action, 0, NULL, 0, NULL, 0)) {
		atomic_set(&confirmation_active, true);
	}
}

void app_confirmation_prompt(bool active, const struct AppConfirmationView *confirmation)
{
	atomic_set(&confirmation_active, active);

	if (!active) {
		app_display_confirmation(NULL);
	} else if (confirmation != NULL) {
		app_display_confirmation(confirmation);
	}
}
