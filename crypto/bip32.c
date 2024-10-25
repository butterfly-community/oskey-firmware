#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <psa/crypto.h>
#include "bip32.h"

int hd_node_from_seed(const uint8_t *seed, int seed_len,
		      uint8_t *master_sk, uint8_t *chain_code)
{
	psa_status_t status;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = 0;
	size_t output_len;
	uint8_t hmac_output[64] = {0};

	// HMAC key
	static const uint8_t hmac_key[] = "Bitcoin seed";
	static const size_t hmac_key_len = sizeof(hmac_key) - 1;

	psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_512));
	psa_set_key_bits(&attributes, PSA_BYTES_TO_BITS(hmac_key_len));

	status = psa_import_key(&attributes, hmac_key, hmac_key_len, &key_id);
	if (status != PSA_SUCCESS)
	{
		psa_destroy_key(key_id);
		memset(hmac_output, 0, sizeof(hmac_output));
		return status;
	}

	status = psa_mac_compute(key_id,
				 PSA_ALG_HMAC(PSA_ALG_SHA_512),
				 seed, seed_len,
				 hmac_output, sizeof(hmac_output),
				 &output_len);

	if (status == PSA_SUCCESS)
	{
		if (output_len != sizeof(hmac_output))
		{
			status = PSA_ERROR_GENERIC_ERROR;
		}
		else
		{
			memcpy(master_sk, hmac_output, 32);
			memcpy(chain_code, hmac_output + 32, 32);
		}
	}

	psa_destroy_key(key_id);
	memset(hmac_output, 0, sizeof(hmac_output));
	return status;
}
