#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <psa/crypto.h>
#include "bip32.h"
#include "codec/base58.h"

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

// xprv (mainnet)
#define MAINNET_PRIVATE_VERSION 0x0488ADE4

// generate BIP32 Extended Key
bool generate_xprv(const uint8_t *master_key, const uint8_t *chain_code, char *xprv_out, size_t *xprv_size)
{
	// 4 bytes: version
	// 1 byte:  depth
	// 4 bytes: parent fingerprint
	// 4 bytes: child number
	// 32 bytes: chain code
	// 33 bytes: private key (1 byte prefix + 32 bytes key)
	uint8_t raw_data[78] = {0};
	size_t offset = 0;

	// 1. version (big-endian)
	raw_data[offset++] = (MAINNET_PRIVATE_VERSION >> 24) & 0xFF;
	raw_data[offset++] = (MAINNET_PRIVATE_VERSION >> 16) & 0xFF;
	raw_data[offset++] = (MAINNET_PRIVATE_VERSION >> 8) & 0xFF;
	raw_data[offset++] = MAINNET_PRIVATE_VERSION & 0xFF;

	// 2. depth
	raw_data[offset++] = 0;

	// 3. parent fingerprint
	offset += 4;

	// 4. child number
	offset += 4;

	// 5. chain code
	memcpy(raw_data + offset, chain_code, 32);
	offset += 32;

	// 6. private key
	raw_data[offset++] = 0x00;
	memcpy(raw_data + offset, master_key, 32);

	// Base58Check
	return b58check_enc_rel(xprv_out, xprv_size, 0, raw_data, sizeof(raw_data));
}
