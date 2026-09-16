/* SPDX-License-Identifier: MPL-2.0 */
#ifndef OSKEY_NXP_SM_PORT_H
#define OSKEY_NXP_SM_PORT_H

#include <stdbool.h>
#include <stdlib.h>

#ifdef OSKEY_NXP_HOST_TEST
#include <pthread.h>
#define sm_malloc                 malloc
#define sm_free                   free
#define SM_MUTEX_DEFINE(x)        pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER
#define SM_MUTEX_EXTERN_DEFINE(x) extern pthread_mutex_t x
#define SM_MUTEX_INIT(x)          ((void)0)
#define SM_MUTEX_DEINIT(x)        ((void)0)
#define SM_MUTEX_LOCK(x)          pthread_mutex_lock(&(x))
#define SM_MUTEX_UNLOCK(x)        pthread_mutex_unlock(&(x))
#else
#include <zephyr/kernel.h>
#define sm_malloc                 k_malloc
#define sm_free                   k_free
#define SM_MUTEX_DEFINE(x)        K_MUTEX_DEFINE(x)
#define SM_MUTEX_EXTERN_DEFINE(x) extern struct k_mutex x
#define SM_MUTEX_INIT(x)          ((void)0)
#define SM_MUTEX_DEINIT(x)        ((void)0)
#define SM_MUTEX_LOCK(x)          k_mutex_lock(&(x), K_FOREVER)
#define SM_MUTEX_UNLOCK(x)        k_mutex_unlock(&(x))
#endif

/* The vendor traces include session keys and plaintext APDUs. Keep them disabled
 * independently of the application's diagnostic logging level. */
#define SMLOG_I(...)      ((void)0)
#define SMLOG_E(...)      ((void)0)
#define SMLOG_W(...)      ((void)0)
#define SMLOG_D(...)      ((void)0)
#define SMLOG_AU8_D(...)  ((void)0)
#define SMLOG_MAU8_D(...) ((void)0)
#define FALSE             false
#define TRUE              true

#endif
