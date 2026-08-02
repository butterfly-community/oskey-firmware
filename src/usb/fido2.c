/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <string.h>
#include <zephyr/authentication/fido2/fido2.h>
#include <zephyr/authentication/fido2/fido2_credentials.h>
#include <zephyr/authentication/fido2/fido2_storage.h>
#include <zephyr/authentication/fido2/fido2_types.h>
#include <zephyr/authentication/fido2/fido2_up.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "bus.h"
#include "fido2_pin.h"

LOG_MODULE_REGISTER(oskey_fido2);

struct fido2_response {
	uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE];
	size_t credential_id_len;
	uint8_t data[FIDO2_ECDSA_SIG_MAX_SIZE];
	size_t data_len;
};

static atomic_t active_request_id;
static atomic_t request_sequence;

struct fido2_request {
	uint8_t command;
	const char *rp_id;
	const uint8_t *user_id;
	size_t user_id_len;
	const char *user_name;
	const char *user_display_name;
};

static struct fido2_request current_request;

static int pin_info_get(void *user_data)
{
	struct oskey_fido_pin_info *info = user_data;
	uint8_t hash[FIDO2_PIN_HASH_SIZE];
	int ret;

	ret = fido2_storage_pin_get(hash);
	mbedtls_platform_zeroize(hash, sizeof(hash));
	if (ret < 0 && ret != -ENOENT) {
		return ret;
	}
	info->set = ret == 0;
	return fido2_storage_pin_retries_get(&info->retries);
}

int oskey_fido_pin_info_get(struct oskey_fido_pin_info *info)
{
	return info == NULL ? -EINVAL : fido2_run_exclusive(pin_info_get, info);
}

struct pin_request {
	const char *pin;
	size_t len;
};

static int pin_set(void *user_data)
{
	const struct pin_request *request = user_data;
	uint8_t hash[FIDO2_SHA256_SIZE];
	size_t hash_len;
	psa_status_t status;
	int ret;

	struct oskey_fido_pin_info info;
	ret = pin_info_get(&info);
	if (ret < 0) {
		return ret;
	}
	if (info.set) {
		return -EALREADY;
	}

	status = psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)request->pin, request->len,
				  hash, sizeof(hash), &hash_len);
	if (status != PSA_SUCCESS || hash_len != FIDO2_SHA256_SIZE) {
		ret = -EIO;
	} else {
		ret = fido2_storage_pin_set(hash);
		if (ret == 0) {
			ret = fido2_storage_pin_retries_reset();
		}
	}

	mbedtls_platform_zeroize(hash, sizeof(hash));
	return ret;
}

int oskey_fido_pin_set(const char *pin, size_t len)
{
	if (pin == NULL || len < CONFIG_FIDO2_MIN_PIN_LENGTH || len > 63) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; ++i) {
		if ((uint8_t)pin[i] < 0x20 || (uint8_t)pin[i] > 0x7e) {
			return -EINVAL;
		}
	}

	struct pin_request request = {
		.pin = pin,
		.len = len,
	};
	return fido2_run_exclusive(pin_set, &request);
}

static uint32_t request_id_next(void)
{
	uint32_t id;

	do {
		id = (uint32_t)atomic_inc(&request_sequence) + 1;
	} while (id == 0);
	return id;
}

static void request_finish(void)
{
	atomic_set(&active_request_id, 0);
}

static void confirmation_cancel(uint32_t request_id)
{
	int ret = app_core_submit_fido(FidoRequestKind_CancelConfirmation, request_id, 0, NULL, 0,
				       NULL, 0, K_FOREVER);

	if (ret < 0) {
		LOG_WRN("Failed to cancel FIDO2 confirmation: %d", ret);
	}
}

static int request(enum FidoRequestKind kind, uint32_t value, const void *data, size_t len,
		   const void *auxiliary, size_t auxiliary_len, struct fido2_response *response)
{
	uint32_t request_id = request_id_next();
	if (!atomic_cas(&active_request_id, 0, request_id)) {
		return -EBUSY;
	}
	if (response != NULL) {
		memset(response, 0, sizeof(*response));
	}

	int ret = app_core_submit_fido(kind, request_id, value, data, len, auxiliary, auxiliary_len,
				       K_NO_WAIT);
	if (ret < 0) {
		request_finish();
		return ret;
	}

	k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(CONFIG_FIDO2_UP_TIMEOUT_MS));
	struct app_fido_result result;

	while ((ret = app_fido_result_get(&result, sys_timepoint_timeout(deadline))) == 0) {
		if (result.request_id != request_id) {
			app_payload_release(result.payload);
			continue;
		}

		size_t payload_len = app_payload_length(result.payload);
		size_t data_len = payload_len >= result.credential_id_len
					  ? payload_len - result.credential_id_len
					  : 0;
		bool valid = result.credential_id_len <= payload_len;

		if (result.status == FidoStatus_Success && response != NULL) {
			valid = valid &&
				result.credential_id_len <= sizeof(response->credential_id) &&
				data_len <= sizeof(response->data) &&
				app_payload_read(result.payload, 0, response->credential_id,
						 result.credential_id_len) ==
					result.credential_id_len &&
				app_payload_read(result.payload, result.credential_id_len,
						 response->data, data_len) == data_len;
			if (valid) {
				response->credential_id_len = result.credential_id_len;
				response->data_len = data_len;
			}
		}

		enum FidoStatus status = result.status;
		app_payload_release(result.payload);
		request_finish();
		if (!valid) {
			return -EIO;
		}
		switch (status) {
		case FidoStatus_Success:
			return 0;
		case FidoStatus_Cancelled:
			return -ECANCELED;
		default:
			return -EACCES;
		}
	}

	if (ret != -EAGAIN) {
		request_finish();
		return ret;
	}
	LOG_ERR("FIDO2 application request timed out");
	if (kind == FidoRequestKind_Confirm) {
		confirmation_cancel(request_id);
	}
	request_finish();
	return -ETIMEDOUT;
}

int fido2_credentials_make(const char *rp_id, uint8_t cred_protect,
			   uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE],
			   uint8_t public_key[FIDO2_P256_UNCOMPRESSED_KEY_SIZE])
{
	struct fido2_response response;
	int ret = request(FidoRequestKind_Register, cred_protect, rp_id, strlen(rp_id), NULL, 0,
			  &response);

	if (ret == 0 && (response.credential_id_len != FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE ||
			 response.data_len != FIDO2_P256_UNCOMPRESSED_KEY_SIZE)) {
		ret = -EIO;
	}
	if (ret == 0) {
		memcpy(credential_id, response.credential_id, response.credential_id_len);
		memcpy(public_key, response.data, response.data_len);
	}
	memset(&response, 0, sizeof(response));
	return ret;
}

int fido2_credentials_validate(const uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE],
			       const uint8_t rp_id_hash[FIDO2_SHA256_SIZE], uint8_t *cred_protect)
{
	struct fido2_response response;
	int ret = request(FidoRequestKind_Validate, 0, credential_id,
			  FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE, rp_id_hash, FIDO2_SHA256_SIZE,
			  &response);
	if (ret == -EACCES) {
		ret = -ENOENT;
	}

	if (ret == 0 && response.data_len != sizeof(uint8_t)) {
		ret = -EIO;
	}
	if (ret == 0 && cred_protect != NULL) {
		*cred_protect = response.data[0];
	}
	memset(&response, 0, sizeof(response));
	return ret;
}

int fido2_credentials_sign(const uint8_t credential_id[FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE],
			   const uint8_t rp_id_hash[FIDO2_SHA256_SIZE],
			   const uint8_t hash[FIDO2_SHA256_SIZE], uint8_t *signature,
			   size_t signature_size, size_t *signature_len)
{
	uint8_t auxiliary[FIDO2_SHA256_SIZE * 2];
	struct fido2_response response;

	memcpy(auxiliary, rp_id_hash, FIDO2_SHA256_SIZE);
	memcpy(auxiliary + FIDO2_SHA256_SIZE, hash, FIDO2_SHA256_SIZE);
	int ret =
		request(FidoRequestKind_Sign, 0, credential_id, FIDO2_NON_DISCOVERABLE_CRED_ID_SIZE,
			auxiliary, sizeof(auxiliary), &response);
	memset(auxiliary, 0, sizeof(auxiliary));

	if (ret == 0 && response.data_len > signature_size) {
		ret = -ENOSPC;
	}
	if (ret == 0) {
		memcpy(signature, response.data, response.data_len);
		*signature_len = response.data_len;
	}
	memset(&response, 0, sizeof(response));
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
	FidoOperation operation;
	const char *rp_id = current_request.rp_id == NULL ? "" : current_request.rp_id;
	const uint8_t *account = current_request.user_id;
	size_t account_len = current_request.user_id_len;

	switch (current_request.command) {
	case FIDO2_CMD_MAKE_CREDENTIAL:
		operation = FidoOperation_Register;
		break;
	case FIDO2_CMD_GET_ASSERTION:
		operation = FidoOperation_Authenticate;
		break;
	case FIDO2_CMD_SELECTION:
		operation = FidoOperation_Select;
		break;
	case FIDO2_CMD_CLIENT_PIN:
		operation = FidoOperation_Authorize;
		if (account_len == 1) {
			if (account[0] == (BIT(0) | BIT(1))) {
				account = (const uint8_t *)"Create and use credentials";
			} else if ((account[0] & BIT(0)) != 0) {
				account = (const uint8_t *)"Create credentials";
			} else {
				account = (const uint8_t *)"Use credentials";
			}
			account_len = strlen((const char *)account);
		}
		break;
	default:
		memset(&current_request, 0, sizeof(current_request));
		return -EINVAL;
	}

	if (current_request.user_name != NULL && current_request.user_name[0] != '\0') {
		account = (const uint8_t *)current_request.user_name;
		account_len = strlen(current_request.user_name);
	}
	if (current_request.user_display_name != NULL &&
	    current_request.user_display_name[0] != '\0') {
		account = (const uint8_t *)current_request.user_display_name;
		account_len = strlen(current_request.user_display_name);
	}

	int ret = request(FidoRequestKind_Confirm, operation, rp_id, strlen(rp_id), account,
			  account_len, NULL);
	memset(&current_request, 0, sizeof(current_request));
	return ret;
}

void fido2_up_cancel(void)
{
	uint32_t request_id = (uint32_t)atomic_get(&active_request_id);

	if (request_id != 0) {
		confirmation_cancel(request_id);
	}
}
