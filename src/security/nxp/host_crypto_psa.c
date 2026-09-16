/* SPDX-License-Identifier: MPL-2.0 */

#include "host_crypto_psa.h"
#include "se05x_scp03_crypto.h"
#include <psa/crypto.h>

struct cmac_context {
	psa_mac_operation_t operation;
	psa_key_id_t key;
	bool used;
};

/* nano-package is serialized and needs one incremental CMAC at a time. */
static struct cmac_context cmac;

void nxp_wipe(void *buffer, size_t length)
{
	volatile uint8_t *p = buffer;
	while (length-- != 0) {
		*p++ = 0;
	}
}

static void cmac_release(struct cmac_context *ctx)
{
	(void)psa_mac_abort(&ctx->operation);
	if (ctx->key != 0) {
		(void)psa_destroy_key(ctx->key);
	}
	nxp_wipe(ctx, sizeof(*ctx));
}

void nxp_crypto_reset(void)
{
	if (cmac.used) {
		cmac_release(&cmac);
	}
}

static struct cmac_context *cmac_context(void *handle)
{
	return handle == &cmac && cmac.used ? &cmac : NULL;
}

static psa_status_t import_aes(const uint8_t *key, size_t len, psa_algorithm_t alg,
			       psa_key_usage_t usage, psa_key_id_t *id)
{
	if (key == NULL || (len != 16 && len != 24 && len != 32)) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, len * 8);
	psa_set_key_usage_flags(&attributes, usage);
	psa_set_key_algorithm(&attributes, alg);
	psa_status_t status = psa_import_key(&attributes, key, len, id);
	psa_reset_key_attributes(&attributes);
	return status;
}

int hcrypto_get_random(uint8_t *buffer, size_t len)
{
	if (buffer == NULL && len != 0) {
		return 1;
	}
	if (psa_generate_random(buffer, len) != PSA_SUCCESS) {
		nxp_wipe(buffer, len);
		return 1;
	}
	return 0;
}

int hcrypto_cmac_oneshot(uint8_t *key, size_t keylen, uint8_t *input, size_t len, uint8_t *output,
			 size_t *output_len)
{
	if (output == NULL || output_len == NULL || *output_len < 16 ||
	    (input == NULL && len != 0)) {
		return 1;
	}
	psa_key_id_t id = 0;
	psa_status_t status =
		import_aes(key, keylen, PSA_ALG_CMAC, PSA_KEY_USAGE_SIGN_MESSAGE, &id);
	size_t written = 0;
	if (status == PSA_SUCCESS) {
		status = psa_mac_compute(id, PSA_ALG_CMAC, input, len, output, 16, &written);
	}
	if (id != 0) {
		(void)psa_destroy_key(id);
	}
	if (status != PSA_SUCCESS || written != 16) {
		nxp_wipe(output, 16);
		*output_len = 0;
		return 1;
	}
	*output_len = written;
	return 0;
}

void *hcrypto_cmac_setup(uint8_t *key, size_t keylen)
{
	if (cmac.used) {
		return NULL;
	}
	cmac.operation = psa_mac_operation_init();
	cmac.used = true;
	if (import_aes(key, keylen, PSA_ALG_CMAC, PSA_KEY_USAGE_SIGN_MESSAGE, &cmac.key) !=
		    PSA_SUCCESS ||
	    psa_mac_sign_setup(&cmac.operation, cmac.key, PSA_ALG_CMAC) != PSA_SUCCESS) {
		cmac_release(&cmac);
		return NULL;
	}
	return &cmac;
}

int hcrypto_cmac_init(void *handle)
{
	return cmac_context(handle) == NULL ? 1 : 0;
}

int hcrypto_cmac_update(void *handle, uint8_t *input, size_t len)
{
	struct cmac_context *ctx = cmac_context(handle);
	if (ctx == NULL) {
		return 1;
	}
	if ((input == NULL && len != 0) ||
	    psa_mac_update(&ctx->operation, input, len) != PSA_SUCCESS) {
		cmac_release(ctx);
		return 1;
	}
	return 0;
}

int hcrypto_cmac_final(void *handle, uint8_t *output, size_t *output_len)
{
	struct cmac_context *ctx = cmac_context(handle);
	if (ctx == NULL) {
		return 1;
	}
	int ret = 1;
	if (output != NULL && output_len != NULL && *output_len >= 16) {
		size_t written = 0;
		if (psa_mac_sign_finish(&ctx->operation, output, 16, &written) == PSA_SUCCESS &&
		    written == 16) {
			*output_len = written;
			ret = 0;
		} else {
			nxp_wipe(output, 16);
			*output_len = 0;
		}
	}
	cmac_release(ctx);
	return ret;
}

static int aes_cbc(bool encrypt, uint8_t *key, size_t keylen, uint8_t *iv, size_t ivlen,
		   const uint8_t *input, uint8_t *output, size_t len)
{
	if (key == NULL || iv == NULL || ivlen != 16 || len % 16 != 0 || len > MAX_APDU_BUFFER ||
	    (len != 0 && (input == NULL || output == NULL))) {
		return 1;
	}
	psa_key_id_t id = 0;
	psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
	uint8_t result[MAX_APDU_BUFFER + 16];
	uint8_t next_iv[16];
	size_t written = 0, final = 0;
	psa_status_t status =
		import_aes(key, keylen, PSA_ALG_CBC_NO_PADDING,
			   encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT, &id);
	if (status != PSA_SUCCESS) {
		goto cleanup;
	}
	if (len == 0) {
		goto cleanup;
	}
	if (!encrypt) {
		memcpy(next_iv, input + len - 16, 16);
	}
	status = encrypt ? psa_cipher_encrypt_setup(&operation, id, PSA_ALG_CBC_NO_PADDING)
			 : psa_cipher_decrypt_setup(&operation, id, PSA_ALG_CBC_NO_PADDING);
	if (status != PSA_SUCCESS ||
	    (status = psa_cipher_set_iv(&operation, iv, 16)) != PSA_SUCCESS ||
	    (status = psa_cipher_update(&operation, input, len, result, sizeof(result),
					&written)) != PSA_SUCCESS ||
	    (status = psa_cipher_finish(&operation, result + written, sizeof(result) - written,
					&final)) != PSA_SUCCESS) {
		goto cleanup;
	}
	if (written + final != len) {
		status = PSA_ERROR_CORRUPTION_DETECTED;
		goto cleanup;
	}
	if (encrypt) {
		memcpy(next_iv, result + len - 16, 16);
	}
	memcpy(output, result, len);
	memcpy(iv, next_iv, 16);
cleanup:
	(void)psa_cipher_abort(&operation);
	if (id != 0) {
		(void)psa_destroy_key(id);
	}
	nxp_wipe(result, sizeof(result));
	nxp_wipe(next_iv, sizeof(next_iv));
	if (status != PSA_SUCCESS && output != NULL) {
		nxp_wipe(output, len);
	}
	return status == PSA_SUCCESS ? 0 : 1;
}

int hcrypto_aes_cbc_encrypt(uint8_t *key, size_t keylen, uint8_t *iv, size_t ivlen,
			    const uint8_t *input, uint8_t *output, size_t len)
{
	return aes_cbc(true, key, keylen, iv, ivlen, input, output, len);
}

int hcrypto_aes_cbc_decrypt(uint8_t *key, size_t keylen, uint8_t *iv, size_t ivlen,
			    const uint8_t *input, uint8_t *output, size_t len)
{
	return aes_cbc(false, key, keylen, iv, ivlen, input, output, len);
}

int nxp_pin_credential(const uint8_t pin_key[32], const uint8_t salt[16], uint8_t credential[16])
{
	static const uint8_t domain[] = "OSKEY/A5000/PIN/V1";
	uint8_t input[sizeof(domain) - 1 + 16];
	uint8_t digest[32];
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = 0;
	size_t written = 0;
	if (pin_key == NULL || salt == NULL || credential == NULL) {
		return 1;
	}
	memcpy(input, domain, sizeof(domain) - 1);
	memcpy(input + sizeof(domain) - 1, salt, 16);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
	psa_status_t status = psa_import_key(&attributes, pin_key, 32, &key);
	if (status == PSA_SUCCESS) {
		status = psa_mac_compute(key, PSA_ALG_HMAC(PSA_ALG_SHA_256), input, sizeof(input),
					 digest, sizeof(digest), &written);
	}
	if (status == PSA_SUCCESS && written == sizeof(digest)) {
		memcpy(credential, digest, 16);
	} else {
		nxp_wipe(credential, 16);
		status = PSA_ERROR_GENERIC_ERROR;
	}
	if (key != 0) {
		(void)psa_destroy_key(key);
	}
	psa_reset_key_attributes(&attributes);
	nxp_wipe(digest, sizeof(digest));
	nxp_wipe(input, sizeof(input));
	return status == PSA_SUCCESS ? 0 : 1;
}
