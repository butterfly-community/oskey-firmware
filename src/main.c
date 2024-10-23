#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <psa/crypto.h>
#include "crypto/bip39.h"
#include "temp/test.h"

extern void rust_main(void);

void cs_random(void *dst, size_t len);

int main(void)
{
	// Rust support test
	rust_main();
	// Lib support test
	test();

	// Gen random
	uint8_t data[32] = {0};
	cs_random(data, (size_t)32);

	// Gen Entropy
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
