#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/random/random.h>
#include "test.h"

void cs_random(void *dst, size_t len);

extern void rust_main(void);

int main(void)
{
	// Rust support test
	rust_main();
	// Lib support test
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
