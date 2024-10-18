#include <stdio.h>
#include <zephyr/random/random.h>
#include "crypto/bip39.h"

int main(void)
{
	const char *mnemonic = mnemonic_generate(128);

	uint8_t seed[512 / 8];

	printf("\nMnemonic: 128 bit: %s\n", mnemonic);

	mnemonic_to_seed(mnemonic, "", seed, NULL);

	printf("Seed: ");

	for (int i = 0; i < sizeof(seed); i++)
	{
		printf("%02x", seed[i]);
	}

	printf("\n\n");

	return 0;
}

uint32_t random32(void)
{
#ifdef CONFIG_ENTROPY_HAS_DRIVER
	uint32_t ret;
	sys_csrand_get(&ret, sizeof(ret));
	return ret;
#else
	return sys_rand32_get();
#endif
}
