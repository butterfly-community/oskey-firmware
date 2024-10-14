#include <stdio.h>
#include "crypto/bip39.h"
#include <zephyr/random/random.h>

int main(void)
{
  const char *mnemonic = mnemonic_generate(128);

  printf("Mnemonic: 128 bit: %s\n", mnemonic);

  return 0;
}

#ifdef CONFIG_ENTROPY_HAS_DRIVER

uint32_t random32(void)
{
  uint32_t ret;

  sys_csrand_get(&ret, sizeof(ret));

  return ret;
}

#else

uint32_t random32(void)
{
  return sys_rand32_get();
}

#endif /* CONFIG_ENTROPY_HAS_DRIVER */
