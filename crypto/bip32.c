#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <psa/crypto.h>
#include "bip32.h"
#include "codec/base58.h"

int hd_node_from_seed(const uint8_t *seed, int seed_len, uint8_t *master_sk, uint8_t *chain_code)
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
	if (status != PSA_SUCCESS) {
		psa_destroy_key(key_id);
		memset(hmac_output, 0, sizeof(hmac_output));
		return status;
	}

	status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_512), seed, seed_len, hmac_output,
				 sizeof(hmac_output), &output_len);

	if (status == PSA_SUCCESS) {
		if (output_len != sizeof(hmac_output)) {
			status = PSA_ERROR_GENERIC_ERROR;
		} else {
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
bool generate_xprv(const uint8_t *master_key, const uint8_t *chain_code, char *xprv_out,
		   size_t *xprv_size)
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

#define HARDENED_INDEX_START 0x80000000

// K256 curve order
static const uint8_t k256_n[32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
				   0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
				   0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};

void print_hex(const char *label, const uint8_t *data, size_t len, bool reverse)
{
	printf("%s: ", label);
	if (reverse) {
		for (int i = len - 1; i >= 0; i--) {
			printf("%02x", data[i]);
		}
	} else {
		for (size_t i = 0; i < len; i++) {
			printf("%02x", data[i]);
		}
	}
	printf("\n");
}

void print_eth_private_key(const char *label, const uint8_t *key)
{
	printf("%s: 0x", label);
	for (int i = 0; i < 32; i++) {
		printf("%02x", key[i]);
	}
	printf("\n");
}

bool hex_to_bytes(const char *hex, uint8_t *bytes, size_t len)
{
	if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
		hex += 2;
	}
	return hex2bin(hex, len * 2, bytes, len) == 0;
}
bool check_sk(const uint8_t *private_key)
{
	static const uint8_t zeros[32] = {0};

	if (memcmp(private_key, zeros, 32) == 0) {
		return false;
	}
	return memcmp(private_key, k256_n, 32) < 0;
}

void k256_add_modulo(uint8_t *result, const uint8_t *num1, const uint8_t *num2)
{
	uint16_t carry = 0;
	uint8_t sum[32];

	for (int i = 31; i >= 0; i--) {
		uint16_t temp = num1[i] + num2[i] + carry;
		sum[i] = temp & 0xFF;
		carry = temp >> 8;
	}

	uint16_t borrow = 0;
	bool need_subtract = (carry || memcmp(sum, k256_n, 32) >= 0);

	for (int i = 31; i >= 0; i--) {
		int16_t diff = sum[i] - (need_subtract ? k256_n[i] : 0) - borrow;
		result[i] = diff & 0xFF;
		borrow = (diff < 0) ? 1 : 0;
	}
}

psa_status_t k256_get_public_key(const uint8_t *private_key, uint8_t *public_key)
{
	psa_status_t status;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id;
	size_t output_length;

	psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_K1));
	psa_set_key_bits(&attributes, 256);
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA_ANY);
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);

	status = psa_import_key(&attributes, private_key, 32, &key_id);
	if (status != PSA_SUCCESS) {
		return status;
	}

	status = psa_export_public_key(key_id, public_key, 65, &output_length);
	psa_destroy_key(key_id);

	return status;
}

psa_status_t bip32_k256_derive_child_key(const extended_key_t *parent, uint32_t index,
					 bool is_hardened, extended_key_t *child)
{
	psa_status_t status;
	uint8_t data[37] = {0};
	size_t data_length;
	uint32_t real_index = index;

	if (!check_sk(parent->private_key)) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	if (is_hardened) {
		if (index >= 0x80000000) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		real_index = index + HARDENED_INDEX_START;

		data[0] = 0x00;
		memcpy(data + 1, parent->private_key, 32);
		data_length = 33;
	} else {
		uint8_t public_key[65];
		status = k256_get_public_key(parent->private_key, public_key);
		if (status != PSA_SUCCESS) {
			return status;
		}

		data[0] = (public_key[64] & 1) ? 0x03 : 0x02;
		memcpy(data + 1, public_key + 1, 32);
		data_length = 33;
	}

	data[data_length++] = (real_index >> 24) & 0xFF;
	data[data_length++] = (real_index >> 16) & 0xFF;
	data[data_length++] = (real_index >> 8) & 0xFF;
	data[data_length++] = real_index & 0xFF;

	psa_key_attributes_t hmac_attrs = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t hmac_key;
	uint8_t hmac_output[64];
	size_t hmac_length;

	psa_set_key_type(&hmac_attrs, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&hmac_attrs, 256);
	psa_set_key_algorithm(&hmac_attrs, PSA_ALG_HMAC(PSA_ALG_SHA_512));
	psa_set_key_usage_flags(&hmac_attrs, PSA_KEY_USAGE_SIGN_MESSAGE);

	status = psa_import_key(&hmac_attrs, parent->chain_code, 32, &hmac_key);
	if (status != PSA_SUCCESS) {
		return status;
	}

	status = psa_mac_compute(hmac_key, PSA_ALG_HMAC(PSA_ALG_SHA_512), data, data_length,
				 hmac_output, sizeof(hmac_output), &hmac_length);
	psa_destroy_key(hmac_key);

	if (status != PSA_SUCCESS) {
		return status;
	}

	if (memcmp(hmac_output, k256_n, 32) >= 0) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	memcpy(child->chain_code, hmac_output + 32, 32);
	k256_add_modulo(child->private_key, hmac_output, parent->private_key);

	if (!check_sk(child->private_key)) {
		return PSA_ERROR_INVALID_ARGUMENT;
	}

	return PSA_SUCCESS;
}

void bip32_test(uint8_t *master_sk, uint8_t *chain_code)
{
	extended_key_t master_key;
	memcpy(master_key.private_key, master_sk, 32);
	memcpy(master_key.chain_code, chain_code, 32);

	print_eth_private_key("Master Private Key", master_key.private_key);
	print_hex("Master Chain Code", master_key.chain_code, 32, false);

	extended_key_t current_key = master_key;
	uint32_t path[] = {44, 60, 0, 0, 1};
	bool is_hardened[] = {true, true, true, false, false};

	for (int i = 0; i < 5; i++) {
		extended_key_t child_key;
		psa_status_t status = bip32_k256_derive_child_key(&current_key, path[i],
								  is_hardened[i], &child_key);
		if (status != PSA_SUCCESS) {
			printf("Derive child key %d, status: %d\n", i + 1, status);
			return 1;
		}
		current_key = child_key;
	}

	print_eth_private_key("End Sk", current_key.private_key);
	print_hex("End Chain Code", current_key.chain_code, 32, false);

	printf("\n\n\n\n\n\n");
}
