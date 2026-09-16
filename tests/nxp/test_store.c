/* SPDX-License-Identifier: MPL-2.0 */
#include "device.h"
#include <psa/crypto.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Persistent chip model: reconnecting never clears auth attempts or objects. */
static uint8_t seed_data[64], salt_data[16], pin_data[16];
static bool has_seed, has_salt, has_pin, authenticated, reset_authenticated;
static unsigned failures, verifies, deletes, calls, fail_at;
static bool delete_lost_reply, delete_noop, configured = true;
static bool fault(void)
{
	return ++calls == fail_at;
}
int nxp_device_open(void)
{
	authenticated = reset_authenticated = false;
	return !configured ? -3 : fault() ? -1 : 0;
}
void nxp_device_close(void)
{
	authenticated = reset_authenticated = false;
}
int nxp_device_exists(uint32_t id, bool *exists)
{
	if (fault()) {
		return -1;
	}
	*exists = id == NXP_PIN_ID ? has_pin : id == NXP_SEED_ID ? has_seed : has_salt;
	return 0;
}
int nxp_device_read(uint32_t id, uint8_t *data, size_t len)
{
	if (fault()) {
		memset(data, 0x55, len);
		return -1;
	}
	if (id == NXP_SEED_ID) {
		assert(len == 64);
		if (!authenticated || !has_seed) {
			return -1;
		}
		memcpy(data, seed_data, len);
	} else {
		assert(id == NXP_SALT_ID && len == 16 && has_salt);
		memcpy(data, salt_data, len);
	}
	return 0;
}
int nxp_device_write(uint32_t id, uint32_t reader, const uint8_t *data, size_t len)
{
	if (fault()) {
		return -1;
	}
	if (id == NXP_SEED_ID) {
		assert(reader == NXP_PIN_ID && len == 64 && !has_seed);
		memcpy(seed_data, data, len);
		has_seed = true;
	} else {
		assert(id == NXP_SALT_ID && reader == 0 && len == 16 && !has_salt);
		memcpy(salt_data, data, len);
		has_salt = true;
	}
	return 0;
}
int nxp_device_user(uint32_t id, const uint8_t value[16], unsigned attempts)
{
	if (fault()) {
		return -1;
	}
	assert(id == NXP_PIN_ID && attempts == 10 && !has_pin);
	memcpy(pin_data, value, 16);
	has_pin = true;
	failures = 0;
	return 0;
}
int nxp_device_auth(uint32_t id, const uint8_t value[16])
{
	++verifies;
	assert(id == NXP_PIN_ID && has_pin);
	if (failures >= 10) {
		return NXP_PIN_REJECTED;
	}
	bool valid = !memcmp(value, pin_data, 16);
	if (!valid) {
		++failures;
	}
	if (fault()) {
		return NXP_ERROR;
	}
	if (!valid) {
		return NXP_PIN_REJECTED;
	}
	failures = 0;
	authenticated = true;
	return 0;
}
int nxp_device_reset_auth(void)
{
	if (fault()) {
		return -1;
	}
	reset_authenticated = true;
	return 0;
}
int nxp_device_delete_all(void)
{
	assert(reset_authenticated);
	++deletes;
	if (fault() || delete_noop) {
		return -1;
	}
	has_seed = has_salt = has_pin = false;
	failures = 0;
	return delete_lost_reply ? -1 : 0;
}
static void zero(const uint8_t *p, size_t n)
{
	while (n--) {
		assert(*p++ == 0);
	}
}
static void fresh(void)
{
	has_seed = has_salt = has_pin = false;
	failures = 0;
	fail_at = calls = 0;
	delete_noop = delete_lost_reply = false;
	configured = true;
	assert(app_nxp_refresh() == 0 && app_nxp_seed_exists() == 0);
}
int main(void)
{
	assert(psa_crypto_init() == PSA_SUCCESS);
	uint8_t pin[32] = {1}, wrong[32] = {2}, seed[64] = {3}, out[64];
	fresh();
	configured = false;
	assert(app_nxp_refresh() == -1 && app_nxp_state() == NXP_UNCONFIGURED);
	fresh();
	assert(!app_nxp_initialize(pin, seed));
	assert(app_nxp_seed_exists() == 1 && app_nxp_initialize(pin, seed));
	assert(!app_nxp_unlock(pin, out) && !memcmp(seed, out, 64));
	assert(app_nxp_unlock(NULL, out));
	zero(out, 64);
	unsigned before = verifies;
	for (unsigned i = 0; i < 9; ++i) {
		assert(app_nxp_unlock(wrong, out) == -2);
		zero(out, 64);
		assert(!app_nxp_refresh()); /* power/reconnect keeps chip counter */
	}
	assert(verifies == before + 9 && failures == 9 && deletes == 0);
	assert(!app_nxp_unlock(pin, out) && failures == 0);
	for (unsigned i = 0; i < 10; ++i) {
		assert(app_nxp_unlock(wrong, out) == -2);
	}
	assert(!app_nxp_refresh());
	assert(app_nxp_unlock(pin, out) == -2 && failures == 10 && has_seed);
	zero(out, 64);
	delete_noop = true;
	assert(app_nxp_erase() && has_seed);
	delete_noop = false;
	delete_lost_reply = true;
	assert(!app_nxp_erase() && !has_seed && app_nxp_seed_exists() == 0);
	assert(!app_nxp_initialize(pin, seed) && !app_nxp_unlock(pin, out));
	/* Fail each operation during provisioning, then recover by whole-wallet erase. */
	fresh();
	calls = 0;
	assert(!app_nxp_initialize(pin, seed));
	unsigned init_calls = calls;
	for (unsigned at = 1; at <= init_calls; ++at) {
		fresh();
		calls = 0;
		fail_at = at;
		assert(app_nxp_initialize(pin, seed));
		fail_at = 0;
		if (has_seed || has_pin || has_salt) {
			assert(app_nxp_seed_exists() != 0);
		}
		assert(!app_nxp_erase());
		assert(!app_nxp_initialize(pin, seed));
	}
	/* Transport faults never release even partly read seed or auto-erase. */
	calls = 0;
	assert(!app_nxp_unlock(pin, out));
	unsigned unlock_calls = calls;
	for (unsigned at = 1; at <= unlock_calls; ++at) {
		calls = 0;
		fail_at = at;
		before = verifies;
		memset(out, 0xaa, sizeof(out));
		assert(app_nxp_unlock(pin, out));
		assert(app_nxp_state() == NXP_UNAVAILABLE);
		zero(out, 64);
		assert(verifies - before <= 1 && has_seed);
		fail_at = 0;
		assert(!app_nxp_refresh());
	}
	puts("persistent lockout, erase recovery, provisioning and unlock fault injection passed");
}
