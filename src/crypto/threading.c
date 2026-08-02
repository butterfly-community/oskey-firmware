#include <mbedtls/threading.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

BUILD_ASSERT(!IS_ENABLED(CONFIG_ENTROPY_PSA_CRYPTO_RNG),
	     "The PSA entropy driver initializes crypto before OSKey threading");

static int mutex_init(mbedtls_platform_mutex_t *mutex)
{
	k_mutex_init(mutex);
	return 0;
}

static void mutex_destroy(mbedtls_platform_mutex_t *mutex)
{
	ARG_UNUSED(mutex);
}

static int mutex_lock(mbedtls_platform_mutex_t *mutex)
{
	return k_mutex_lock(mutex, K_FOREVER);
}

static int mutex_unlock(mbedtls_platform_mutex_t *mutex)
{
	return k_mutex_unlock(mutex);
}

static int cond_init(mbedtls_platform_condition_variable_t *cond)
{
	k_condvar_init(cond);
	return 0;
}

static void cond_destroy(mbedtls_platform_condition_variable_t *cond)
{
	ARG_UNUSED(cond);
}

static int cond_signal(mbedtls_platform_condition_variable_t *cond)
{
	return k_condvar_signal(cond);
}

static int cond_broadcast(mbedtls_platform_condition_variable_t *cond)
{
	return k_condvar_broadcast(cond);
}

static int cond_wait(mbedtls_platform_condition_variable_t *cond, mbedtls_platform_mutex_t *mutex)
{
	return k_condvar_wait(cond, mutex, K_FOREVER);
}

static int crypto_threading_init(void)
{
	mbedtls_threading_set_alt(mutex_init, mutex_destroy, mutex_lock, mutex_unlock, cond_init,
				  cond_destroy, cond_signal, cond_broadcast, cond_wait);
	return 0;
}

SYS_INIT(crypto_threading_init, POST_KERNEL, 0);
