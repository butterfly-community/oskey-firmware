#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <psa/crypto.h>
#include "temp/test.h"

extern void rust_main(void);

int main(void)
{
	rust_main();
	test();
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
