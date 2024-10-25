#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <psa/crypto.h>
#include "crypto/bip39.h"
#include "crypto/bip32.h"
#include "temp/test.h"

extern void rust_main(void);

void cs_random(void *dst, size_t len);

int main(void)
{
	// Init Crypto
	psa_status_t status = psa_crypto_init();
	if (status != PSA_SUCCESS)
	{
		return status;
	}
	// Rust support test
	rust_main();
	// Lib support test
	test();

	// Gen random
	uint8_t data[32] = {0};
	cs_random(data, (size_t)32);

	// Gen Entropy
	printf("\n");
	printf("BIP39 Entropy (hex): ");
	for (size_t i = 0; i < sizeof(data); i++)
	{
		printf("%02x", data[i]);
	}
	printf("\n");

	// Gen mnemonic
	const char *mnemonic = mnemonic_from_data(data, 128);
	printf("Mnemonic: %s\n", mnemonic);

	// Gen seed
	uint8_t seed[512 / 8] = {0};
	mnemonic_to_seed(mnemonic, "", seed);
	printf("BIP39 Seed (hex): ");
	for (size_t i = 0; i < sizeof(seed); i++)
	{
		printf("%02x", seed[i]);
	}
	printf("\n");

	// BIP32 Master key and chain code
	uint8_t master_sk[32] = {0};
	uint8_t chain_code[32] = {0};
	printf("BIP39 Master (hex): ");
	hd_node_from_seed(seed, sizeof(seed), master_sk, chain_code);
	for (size_t i = 0; i < sizeof(master_sk); i++)
	{
		printf("%02x", master_sk[i]);
	}
	printf("\n");
	printf("BIP39 Chain Code (hex): ");
	for (size_t i = 0; i < sizeof(chain_code); i++)
	{
		printf("%02x", chain_code[i]);
	}
	printf("\n");
	return 0;
}

void cs_random(void *dst, size_t len)
{
	sys_csrand_get(dst, len);
}

void rust_panic_wrap(void)
{
	k_panic();
}
