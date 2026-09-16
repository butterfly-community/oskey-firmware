/* SPDX-License-Identifier: MPL-2.0 */
#include "host_crypto_psa.h"
#include "se05x_scp03_crypto.h"
#include <psa/crypto.h>
#include <assert.h>
#include <stdio.h>

static void hex(const char *s, uint8_t *out)
{
	while (*s) {
		unsigned n;
		assert(sscanf(s, "%2x", &n) == 1);
		*out++ = n;
		s += 2;
	}
}

int main(void)
{
	assert(psa_crypto_init() == PSA_SUCCESS);
	uint8_t key[32], data[64], expected[64], out[64], iv[16], original_iv[16];
	hex("2b7e151628aed2a6abf7158809cf4f3c", key);
	hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
	    "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",
	    data);
	/* NIST SP 800-38B: empty, complete, partial, and multiple block CMAC. */
	const size_t lengths[] = {0, 16, 40, 64};
	const char *macs[] = {
		"bb1d6929e95937287fa37d129b756746", "070a16b46b4d4144f79bdd9dd04a287c",
		"dfa66747de9ae63030ca32611497c827", "51f0bebf7e3b9d92fc49741779363cfe"};
	for (unsigned i = 0; i < 4; ++i) {
		size_t n = 16;
		hex(macs[i], expected);
		assert(hcrypto_cmac_oneshot(key, 16, data, lengths[i], out, &n) == 0);
		assert(n == 16 && !memcmp(expected, out, 16));
		void *ctx = hcrypto_cmac_setup(key, 16);
		assert(ctx);
		assert(hcrypto_cmac_init(ctx) == 0);
		for (size_t j = 0; j < lengths[i]; ++j) {
			assert(hcrypto_cmac_update(ctx, data + j, 1) == 0);
		}
		assert(hcrypto_cmac_final(ctx, out, &n) == 0 && !memcmp(expected, out, 16));
		assert(hcrypto_cmac_update(ctx, data, 1) != 0);
	}
	/* NIST SP 800-38A CBC, including in-place decryption and IV chaining. */
	hex("000102030405060708090a0b0c0d0e0f", original_iv);
	hex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
	    "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7",
	    expected);
	memcpy(iv, original_iv, 16);
	assert(!hcrypto_aes_cbc_encrypt(key, 16, iv, 16, data, out, 64));
	assert(!memcmp(expected, out, 64) && !memcmp(iv, expected + 48, 16));
	memcpy(iv, original_iv, 16);
	assert(!hcrypto_aes_cbc_decrypt(key, 16, iv, 16, out, out, 64));
	assert(!memcmp(data, out, 64) && !memcmp(iv, expected + 48, 16));
	assert(hcrypto_aes_cbc_encrypt(key, 15, iv, 16, data, out, 64));
	for (size_t i = 0; i < 64; ++i) {
		assert(out[i] == 0);
	}
	assert(hcrypto_aes_cbc_encrypt(key, 16, iv, 15, data, out, 64));
	assert(hcrypto_aes_cbc_encrypt(key, 16, iv, 16, data, out, 17));
	assert(hcrypto_get_random(NULL, 1));
	assert(!hcrypto_get_random(out, sizeof(out)));
	for (unsigned i = 0; i < 1000; ++i) {
		void *a = hcrypto_cmac_setup(key, 16);
		assert(a && !hcrypto_cmac_setup(key, 16));
		nxp_crypto_reset();
		assert(hcrypto_cmac_init(a));
	}
	uint8_t pin[32] = {1}, salt[16] = {2}, a[16], b[16];
	assert(!nxp_pin_credential(pin, salt, a));
	assert(!nxp_pin_credential(pin, salt, b) && !memcmp(a, b, 16));
	salt[0] ^= 1;
	assert(!nxp_pin_credential(pin, salt, b) && memcmp(a, b, 16));
	puts("PSA crypto vectors and resource lifecycle passed");
}
