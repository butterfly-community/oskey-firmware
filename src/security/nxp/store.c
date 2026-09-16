/* SPDX-License-Identifier: MPL-2.0 */
#include "device.h"
#include "host_crypto_psa.h"
#include "sm_port.h"
#include "se05x_scp03_crypto.h"
#include <stdatomic.h>

SM_MUTEX_DEFINE(nxp_store_mutex);
static _Atomic enum nxp_state state = NXP_UNAVAILABLE;
static bool scanned;

bool app_nxp_enabled(void)
{
	return true;
}

static int scan(void)
{
	bool pin, seed, salt;
	int rc = nxp_device_open();
	if (rc != NXP_OK) {
		state = rc == NXP_CREDENTIALS_MISSING ? NXP_UNCONFIGURED : NXP_UNAVAILABLE;
		return NXP_ERROR;
	}
	if (nxp_device_exists(NXP_PIN_ID, &pin) || nxp_device_exists(NXP_SEED_ID, &seed) ||
	    nxp_device_exists(NXP_SALT_ID, &salt)) {
		state = NXP_UNAVAILABLE;
		return NXP_ERROR;
	}
	state = !pin && !seed && !salt ? NXP_EMPTY
		: pin && seed && salt  ? NXP_READY
				       : NXP_INCOMPLETE;
	return state == NXP_INCOMPLETE ? NXP_ERROR : NXP_OK;
}

int app_nxp_refresh(void)
{
	SM_MUTEX_LOCK(nxp_store_mutex);
	int rc = scan();
	scanned = true;
	nxp_device_close();
	SM_MUTEX_UNLOCK(nxp_store_mutex);
	return rc;
}

int app_nxp_state(void)
{
	/* UI reads a snapshot and never waits for an I2C transaction. */
	return atomic_load(&state);
}

int app_nxp_seed_exists(void)
{
	SM_MUTEX_LOCK(nxp_store_mutex);
	if (!scanned) {
		(void)scan();
		scanned = true;
		nxp_device_close();
	}
	int result = state == NXP_EMPTY                                 ? 0
		     : state == NXP_READY || state == NXP_AUTH_REJECTED ? 1
									: NXP_ERROR;
	SM_MUTEX_UNLOCK(nxp_store_mutex);
	return result;
}

int app_nxp_initialize(const uint8_t pin_key[32], const uint8_t seed[64])
{
	if (!pin_key || !seed) {
		return NXP_ERROR;
	}
	uint8_t salt[16] = {0}, credential[16] = {0}, check[64] = {0};
	int rc = NXP_ERROR;
	SM_MUTEX_LOCK(nxp_store_mutex);
	/* Re-read before writing: partial initialization is never treated as empty.
	 * Objects cannot be individually replaced; recovery requires DeleteAll. */
	if (scan() || state != NXP_EMPTY) {
		goto done;
	}
	if (nxp_device_reset_auth()) {
		state = NXP_UNAVAILABLE;
		goto done;
	}
	if (nxp_device_open() || hcrypto_get_random(salt, sizeof(salt)) ||
	    nxp_pin_credential(pin_key, salt, credential)) {
		state = NXP_UNAVAILABLE;
		goto done;
	}
	state = NXP_INCOMPLETE;
	if (nxp_device_write(NXP_SALT_ID, 0, salt, sizeof(salt)) ||
	    nxp_device_user(NXP_PIN_ID, credential, NXP_PIN_LIMIT) ||
	    nxp_device_write(NXP_SEED_ID, NXP_PIN_ID, seed, 64) ||
	    nxp_device_auth(NXP_PIN_ID, credential) ||
	    nxp_device_read(NXP_SEED_ID, check, sizeof(check))) {
		goto done;
	}
	uint8_t diff = 0;
	for (size_t i = 0; i < sizeof(check); ++i) {
		diff |= check[i] ^ seed[i];
	}
	if (diff) {
		goto done;
	}
	state = NXP_READY;
	rc = NXP_OK;
done:
	scanned = true;
	nxp_device_close();
	nxp_wipe(salt, sizeof(salt));
	nxp_wipe(credential, sizeof(credential));
	nxp_wipe(check, sizeof(check));
	SM_MUTEX_UNLOCK(nxp_store_mutex);
	return rc;
}

int app_nxp_unlock(const uint8_t pin_key[32], uint8_t seed[64])
{
	if (seed) {
		nxp_wipe(seed, 64);
	}
	if (!pin_key || !seed) {
		return NXP_ERROR;
	}
	uint8_t salt[16] = {0}, credential[16] = {0};
	int rc = NXP_ERROR;
	SM_MUTEX_LOCK(nxp_store_mutex);
	if (scan() || state != NXP_READY) {
		goto done;
	}
	if (nxp_device_read(NXP_SALT_ID, salt, sizeof(salt)) ||
	    nxp_pin_credential(pin_key, salt, credential)) {
		state = NXP_UNAVAILABLE;
		goto done;
	}
	/* Exactly one verification per user submission. Never retry authentication
	 * after a timeout: the device may already have consumed the attempt. */
	rc = nxp_device_auth(NXP_PIN_ID, credential);
	if (rc != NXP_OK) {
		state = rc == NXP_PIN_REJECTED ? NXP_AUTH_REJECTED : NXP_UNAVAILABLE;
		goto done;
	}
	rc = nxp_device_read(NXP_SEED_ID, seed, 64);
	if (rc) {
		state = NXP_UNAVAILABLE;
	}
done:
	if (rc) {
		nxp_wipe(seed, 64);
	}
	nxp_device_close();
	nxp_wipe(salt, sizeof(salt));
	nxp_wipe(credential, sizeof(credential));
	SM_MUTEX_UNLOCK(nxp_store_mutex);
	return rc;
}

int app_nxp_erase(void)
{
	int rc = NXP_ERROR;
	SM_MUTEX_LOCK(nxp_store_mutex);
	if (nxp_device_open() || nxp_device_reset_auth()) {
		goto done;
	}
	/* DeleteAll closes sessions and can return without RMAC. Its response is
	 * not proof of deletion; establish a fresh channel and check every object. */
	(void)nxp_device_delete_all();
	if (scan() == NXP_OK && state == NXP_EMPTY) {
		rc = NXP_OK;
	}
done:
	if (rc) {
		state = NXP_UNAVAILABLE;
	}
	scanned = true;
	nxp_device_close();
	SM_MUTEX_UNLOCK(nxp_store_mutex);
	return rc;
}
