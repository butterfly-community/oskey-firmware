/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fido2.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/authentication/fido2/fido2_credentials.h>
#include <zephyr/authentication/fido2/fido2_types.h>
#include <zephyr/authentication/fido2/fido2_up.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "confirmation.h"
#include "message.h"

LOG_MODULE_REGISTER(oskey_fido2);

enum message_state {
	MESSAGE_IDLE,
	MESSAGE_WAITING,
	MESSAGE_TIMED_OUT,
};

struct fido2_response {
	bool success;
	uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE];
	size_t credential_id_len;
	uint8_t data[FIDO2_ECDSA_SIG_MAX_SIZE];
	size_t data_len;
};

static K_SEM_DEFINE(message_complete, 0, 1);
static atomic_t message_state;
static struct fido2_response response;

struct fido2_request {
	uint8_t command;
	const char *rp_id;
	const uint8_t *user_id;
	size_t user_id_len;
	const char *user_name;
	const char *user_display_name;
};

static struct fido2_request current_request;

static const char *prompt_title(uint8_t command)
{
	switch (command) {
	case FIDO2_CMD_MAKE_CREDENTIAL:
		return "Create security key";
	case FIDO2_CMD_GET_ASSERTION:
		return "Use security key";
	case FIDO2_CMD_SELECTION:
		return "Select OSKey";
	default:
		return "Confirm presence";
	}
}

static size_t format_confirmation_prompt(char *prompt)
{
	char user_id[FIDO2_USER_ID_MAX_SIZE * 2 + 3];
	const char *account = current_request.user_display_name;
	const char *account_label = "";

	if (account == NULL || account[0] == '\0') {
		account = current_request.user_name;
	}
	if (account != NULL && account[0] != '\0') {
		account_label = "Account: ";
	}
	if ((account == NULL || account[0] == '\0') && current_request.user_id_len > 0) {
		bool printable = true;

		for (size_t i = 0; i < current_request.user_id_len; i++) {
			if (current_request.user_id[i] < ' ' || current_request.user_id[i] > '~') {
				printable = false;
				break;
			}
		}
		if (printable) {
			memcpy(user_id, current_request.user_id, current_request.user_id_len);
			user_id[current_request.user_id_len] = '\0';
		} else {
			memcpy(user_id, "0x", 2);
			bin2hex(current_request.user_id, current_request.user_id_len, &user_id[2],
				sizeof(user_id) - 2);
		}
		account = user_id;
		account_label = "User ID: ";
	}

	const char *rp_id = current_request.rp_id == NULL ? "" : current_request.rp_id;
	account = account == NULL ? "" : account;
	int written;

	if (current_request.command == FIDO2_CMD_SELECTION) {
		written = snprintf(prompt, APP_MESSAGE_DATA_SIZE, "%s",
				   prompt_title(current_request.command));
	} else if (strcmp(rp_id, "ssh:") == 0) {
		written = snprintf(prompt, APP_MESSAGE_DATA_SIZE,
				   "%s | Service: SSH (ssh:) | Host: Not provided by SSH%s%s%s",
				   prompt_title(current_request.command),
				   account[0] == '\0' ? "" : " | ", account_label, account);
	} else {
		written = snprintf(prompt, APP_MESSAGE_DATA_SIZE, "%s%s%s%s%s%s",
				   prompt_title(current_request.command),
				   rp_id[0] == '\0' ? "" : " | Service: ", rp_id,
				   account[0] == '\0' ? "" : " | ", account_label, account);
	}

	size_t len = written < 0 ? 0 : MIN((size_t)written, APP_MESSAGE_DATA_SIZE - 1);
	for (size_t i = 0; i < len; i++) {
		unsigned char value = prompt[i];

		if (value < ' ' || value == 0x7f) {
			prompt[i] = '?';
		}
	}
	if (written >= APP_MESSAGE_DATA_SIZE) {
		LOG_WRN("FIDO2 confirmation text was truncated");
	}
	return len;
}

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

void fido2_up_set_request(uint8_t command, const char *rp_id, const uint8_t *user_id,
			  size_t user_id_len, const char *user_name, const char *user_display_name)
{
	current_request = (struct fido2_request){
		.command = command,
		.rp_id = rp_id,
		.user_id = user_id,
		.user_id_len = user_id_len,
		.user_name = user_name,
		.user_display_name = user_display_name,
	};
}

int fido2_up_wait(void)
{
	char prompt[APP_MESSAGE_DATA_SIZE];
	size_t len = format_confirmation_prompt(prompt);

	LOG_DBG("FIDO2 confirmation:\n%s", prompt);
	return app_confirmation_wait(AppMessageSource_Fido2, CONFIG_FIDO2_UP_TIMEOUT_MS, prompt,
				     len);
}

void fido2_up_cancel(void)
{
	app_confirmation_cancel(AppMessageSource_Fido2);
}
