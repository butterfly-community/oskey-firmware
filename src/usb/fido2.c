/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fido2.h"

#include <errno.h>
#include <string.h>
#include <zephyr/authentication/fido2/fido2_credentials.h>
#include <zephyr/authentication/fido2/fido2_up.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "confirmation.h"
#include "message.h"

LOG_MODULE_REGISTER(oskey_fido2);

#define FIDO2_SIGNATURE_SIZE 72

enum message_state {
	MESSAGE_IDLE,
	MESSAGE_WAITING,
	MESSAGE_TIMED_OUT,
};

struct fido2_response {
	bool success;
	uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE];
	size_t credential_id_len;
	uint8_t data[FIDO2_SIGNATURE_SIZE];
	size_t data_len;
};

static K_SEM_DEFINE(message_complete, 0, 1);
static atomic_t message_state;
static struct fido2_response response;

static int request(AppMessageAction action, const void *data, size_t len, const void *auxiliary,
		   size_t auxiliary_len)
{
	if (!atomic_cas(&message_state, MESSAGE_IDLE, MESSAGE_WAITING)) {
		return -EBUSY;
	}

	k_sem_reset(&message_complete);
	memset(&response, 0, sizeof(response));

	if (!app_message_submit(AppMessageSource_Fido2, action, 0, data, len, auxiliary,
				auxiliary_len)) {
		atomic_set(&message_state, MESSAGE_IDLE);
		return -EIO;
	}

	if (k_sem_take(&message_complete, K_MSEC(CONFIG_FIDO2_UP_TIMEOUT_MS)) != 0) {
		if (atomic_cas(&message_state, MESSAGE_WAITING, MESSAGE_TIMED_OUT)) {
			LOG_ERR("FIDO2 application request timed out");
			return -ETIMEDOUT;
		}
		k_sem_take(&message_complete, K_FOREVER);
	}
	return response.success ? 0 : -EACCES;
}

void fido2_message_reply(bool success, const uint8_t *credential_id, size_t credential_id_len,
			 const uint8_t *data, size_t data_len)
{
	if (atomic_get(&message_state) == MESSAGE_IDLE) {
		return;
	}

	if (credential_id_len > sizeof(response.credential_id) ||
	    data_len > sizeof(response.data)) {
		success = false;
	} else if (success) {
		if (credential_id_len > 0) {
			memcpy(response.credential_id, credential_id, credential_id_len);
		}
		if (data_len > 0) {
			memcpy(response.data, data, data_len);
		}
		response.credential_id_len = credential_id_len;
		response.data_len = data_len;
	}

	response.success = success;
	bool waiting = atomic_cas(&message_state, MESSAGE_WAITING, MESSAGE_IDLE);

	k_sem_give(&message_complete);
	if (!waiting) {
		atomic_set(&message_state, MESSAGE_IDLE);
	}
}

int fido2_credentials_make(const char *rp_id, const uint8_t *user_id, size_t user_id_len,
			   uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE],
			   uint8_t public_key[FIDO2_P256_UNCOMPRESSED_KEY_SIZE])
{
	int ret =
		request(AppMessageAction_Fido2Register, rp_id, strlen(rp_id), user_id, user_id_len);

	if (ret == 0 && (response.credential_id_len != FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE ||
			 response.data_len != FIDO2_P256_UNCOMPRESSED_KEY_SIZE)) {
		ret = -EIO;
	}
	if (ret == 0) {
		memcpy(credential_id, response.credential_id, response.credential_id_len);
		memcpy(public_key, response.data, response.data_len);
	}
	return ret;
}

int fido2_credentials_validate(const uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE],
			       const uint8_t rp_id_hash[FIDO2_SHA256_SIZE])
{
	return request(AppMessageAction_Fido2Sign, credential_id,
		       FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE, rp_id_hash, FIDO2_SHA256_SIZE);
}

int fido2_credentials_sign(const uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE],
			   const uint8_t rp_id_hash[FIDO2_SHA256_SIZE],
			   const uint8_t hash[FIDO2_SHA256_SIZE], uint8_t *signature,
			   size_t signature_size, size_t *signature_len)
{
	uint8_t auxiliary[FIDO2_SHA256_SIZE * 2];

	memcpy(auxiliary, rp_id_hash, FIDO2_SHA256_SIZE);
	memcpy(auxiliary + FIDO2_SHA256_SIZE, hash, FIDO2_SHA256_SIZE);
	int ret = request(AppMessageAction_Fido2Sign, credential_id,
			  FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE, auxiliary, sizeof(auxiliary));

	if (ret == 0 && response.data_len > signature_size) {
		ret = -ENOSPC;
	}
	if (ret == 0) {
		memcpy(signature, response.data, response.data_len);
		*signature_len = response.data_len;
	}
	return ret;
}

int fido2_up_wait(void)
{
	LOG_INF("Press the user button to confirm the FIDO2 request");
	return app_confirmation_wait(AppMessageSource_Fido2, CONFIG_FIDO2_UP_TIMEOUT_MS);
}

void fido2_up_cancel(void)
{
	app_confirmation_cancel(AppMessageSource_Fido2);
}
