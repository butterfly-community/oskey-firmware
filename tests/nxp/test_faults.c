/* SPDX-License-Identifier: MPL-2.0 */
#include "host_crypto_psa.h"
#include "se05x_scp03_crypto.h"
#include <psa/crypto.h>
#include <assert.h>
#include <stdio.h>

static unsigned calls, fail_at, live_keys;
static bool fault(void)
{
	return ++calls == fail_at;
}
#define WRAP(name, args, values)                                                                   \
	__typeof__(name) __real_##name;                                                            \
	psa_status_t __wrap_##name args                                                            \
	{                                                                                          \
		return fault() ? PSA_ERROR_INSUFFICIENT_MEMORY : __real_##name values;             \
	}
__typeof__(psa_import_key) __real_psa_import_key;
psa_status_t __wrap_psa_import_key(const psa_key_attributes_t *attr, const uint8_t *data,
				   size_t len, mbedtls_svc_key_id_t *key)
{
	if (fault()) {
		return PSA_ERROR_INSUFFICIENT_MEMORY;
	}
	psa_status_t rc = __real_psa_import_key(attr, data, len, key);
	if (rc == PSA_SUCCESS) {
		++live_keys;
	}
	return rc;
}
__typeof__(psa_destroy_key) __real_psa_destroy_key;
psa_status_t __wrap_psa_destroy_key(mbedtls_svc_key_id_t key)
{
	psa_status_t rc = __real_psa_destroy_key(key);
	assert(rc == PSA_SUCCESS && live_keys > 0);
	--live_keys;
	return rc;
}
WRAP(psa_generate_random, (uint8_t *out, size_t n), (out, n))
WRAP(psa_mac_compute,
     (mbedtls_svc_key_id_t key, psa_algorithm_t alg, const uint8_t *in, size_t n, uint8_t *out,
      size_t cap, size_t *len),
     (key, alg, in, n, out, cap, len))
WRAP(psa_mac_sign_setup, (psa_mac_operation_t * op, mbedtls_svc_key_id_t key, psa_algorithm_t alg),
     (op, key, alg))
WRAP(psa_mac_update, (psa_mac_operation_t * op, const uint8_t *in, size_t len), (op, in, len))
WRAP(psa_mac_sign_finish, (psa_mac_operation_t * op, uint8_t *out, size_t cap, size_t *len),
     (op, out, cap, len))
WRAP(psa_cipher_encrypt_setup,
     (psa_cipher_operation_t * op, mbedtls_svc_key_id_t key, psa_algorithm_t alg), (op, key, alg))
WRAP(psa_cipher_decrypt_setup,
     (psa_cipher_operation_t * op, mbedtls_svc_key_id_t key, psa_algorithm_t alg), (op, key, alg))
WRAP(psa_cipher_set_iv, (psa_cipher_operation_t * op, const uint8_t *iv, size_t len), (op, iv, len))
WRAP(psa_cipher_update,
     (psa_cipher_operation_t * op, const uint8_t *in, size_t len, uint8_t *out, size_t cap,
      size_t *written),
     (op, in, len, out, cap, written))
WRAP(psa_cipher_finish, (psa_cipher_operation_t * op, uint8_t *out, size_t cap, size_t *written),
     (op, out, cap, written))

static int run(unsigned which)
{
	uint8_t key[32] = {1}, iv[16] = {0}, input[32] = {2}, output[32] = {0};
	size_t len = 16;
	switch (which) {
	case 0:
		return hcrypto_cmac_oneshot(key, 16, input, 32, output, &len);
	case 1: {
		void *ctx = hcrypto_cmac_setup(key, 16);
		if (!ctx) {
			return 1;
		}
		if (hcrypto_cmac_update(ctx, input, 32)) {
			return 1;
		}
		return hcrypto_cmac_final(ctx, output, &len);
	}
	case 2:
		return hcrypto_aes_cbc_encrypt(key, 16, iv, 16, input, output, 32);
	case 3:
		return hcrypto_aes_cbc_decrypt(key, 16, iv, 16, input, output, 32);
	case 4:
		return nxp_pin_credential(key, iv, output);
	default:
		return hcrypto_get_random(output, 32);
	}
}
int main(void)
{
	assert(psa_crypto_init() == PSA_SUCCESS);
	for (unsigned which = 0; which < 6; ++which) {
		calls = fail_at = 0;
		assert(!run(which));
		unsigned count = calls;
		assert(live_keys == 0);
		for (unsigned at = 1; at <= count; ++at) {
			calls = 0;
			fail_at = at;
			assert(run(which));
			nxp_crypto_reset();
			assert(live_keys == 0);
			fail_at = 0;
			assert(!run(which));
			assert(live_keys == 0);
		}
	}
	puts("PSA allocation/operation fault injection and key cleanup passed");
}
