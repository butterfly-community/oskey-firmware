#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include "bip39.h"

#ifdef CONFIG_RUST

extern void rust_main(void);

int main(void)
{
	rust_main();

	const char *mnemonic = mnemonic_generate(128);

	uint8_t seed[512 / 8];

	printf("\nMnemonic: 128 bit: %s\n", mnemonic);

	mnemonic_to_seed(mnemonic, "", seed, NULL);

	for (int i = 0; i < sizeof(seed); i++)
	{
		printf("%02x", seed[i]);
	}
	printf("\n\n");

	return 0;
}

uint32_t random32(void)
{
	uint32_t ret;
	sys_csrand_get(&ret, sizeof(ret));
	return ret;
}

void rust_panic_wrap(void)
{
	k_panic();
}

#endif
