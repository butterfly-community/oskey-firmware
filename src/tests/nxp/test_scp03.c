/* SPDX-License-Identifier: MPL-2.0 */
#include <stdbool.h>
#include "se05x_APDU_apis.h"
#include "se05x_scp03.h"
#include <psa/crypto.h>
#include <assert.h>
#include <stdio.h>

static size_t hex(const char *s, uint8_t *out)
{
	size_t len = 0;
	while (*s) {
		unsigned n;
		assert(sscanf(s, "%2x", &n) == 1);
		out[len++] = n;
		s += 2;
	}
	return len;
}
static Se05xSession_t channel(void)
{
	Se05xSession_t s = {.scp03_session = 1, .applet_version = 0x07020000};
	for (unsigned i = 0; i < 16; ++i) {
		s.scp03_session_enc_Key[i] = i;
		s.scp03_session_mac_Key[i] = 16 + i;
		s.scp03_session_rmac_Key[i] = 32 + i;
	}
	s.scp03_counter[15] = 1;
	return s;
}
int main(void)
{
	assert(psa_crypto_init() == PSA_SUCCESS);
	uint8_t command[255], expected[255], response[255], out[255];
	tlvHeader_t header = {{0x80, 2, 0, 0}};
	/* Independent AES/CMAC fixtures for GP SCP03, counter=1, zero initial MCV,
	 * S-ENC=00..0f, S-MAC=10..1f, S-RMAC=20..2f. */
	size_t expected_len =
		hex("84020000187b8fffd7a14d0cec991dc047d1bbf50c18551b3c85ff6d6a", expected);
	for (int corrupt = -1; corrupt < 26; ++corrupt) {
		Se05xSession_t s = channel();
		size_t n = hex("41044f534b02", command);
		assert(Se05x_API_SCP03_Encrypt(&s, &header, command, n, 0, command, &n) == SM_OK);
		assert(n == expected_len && !memcmp(command, expected, n));
		size_t r = hex("7527f90525d2ee18a8a78f390bf639188a82ebf59aec38d49000", response);
		if (corrupt >= 0) {
			response[corrupt] ^= 1;
		}
		size_t len = sizeof(out);
		smStatus_t rc = Se05x_API_SCP03_Decrypt(&s, 6, response, r, out, &len);
		if (corrupt < 0) {
			assert(rc == SM_OK && len == 7 &&
			       !memcmp(out, (uint8_t[]){0x41, 3, 1, 2, 3, 0x90, 0}, 7));
			assert(s.scp03_counter[15] == 2);
		} else {
			assert(rc != SM_OK);
		}
	}
	puts("nano-package SCP03/PSA command and response vectors, tamper rejection passed");
}
