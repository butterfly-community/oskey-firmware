/* SPDX-License-Identifier: MPL-2.0 */
#include "device.h"
#include "se05x_APDU_apis.h"
#include "se05x_scp03.h"
#include "smCom.h"
#include "phNxpEse_Api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t header[4], payload[255], plain[255];
static size_t payload_len, plain_len, wire_len = 10;
static bool bad_mac, fail_transport, no_scp, configured = true;
static bool valid_auth;
static uint16_t status = 0x9000;
static unsigned transactions;
bool nxp_credentials_load(struct nxp_credentials *keys)
{
	memset(keys, 1, sizeof(*keys));
	return configured;
}
smStatus_t Se05x_API_SessionOpen(pSe05xSession_t s)
{
	s->scp03_session = !no_scp;
	return SM_OK;
}
smStatus_t Se05x_API_SessionClose(pSe05xSession_t s)
{
	(void)s;
	return SM_OK;
}
ESESTATUS phNxpEse_close(void *ctx)
{
	(void)ctx;
	return ESESTATUS_SUCCESS;
}
/* Mock the secure transport boundary so protocol validation can be fault-injected
 * independently of the PSA vector suite. */
smStatus_t Se05x_API_SCP03_Encrypt(pSe05xSession_t s, const tlvHeader_t *h, uint8_t *data,
				   size_t len, uint8_t le, uint8_t *out, size_t *outlen)
{
	(void)le;
	(void)out;
	assert(s->scp03_session);
	memcpy(header, h->hdr, 4);
	memcpy(payload, data, len);
	payload_len = len;
	*outlen = len;
	return SM_OK;
}
smStatus_t smComT1oI2C_TransceiveRaw(void *ctx, uint8_t *tx, size_t txlen, uint8_t *rx,
				     size_t *rxlen)
{
	(void)ctx;
	(void)tx;
	(void)txlen;
	++transactions;
	if (fail_transport) {
		return SM_NOT_OK;
	}
	if (valid_auth && header[1] == 4 && header[3] == 0x1b) {
		plain[0] = 0x41;
		plain[1] = 8;
		memset(plain + 2, 0x77, 8);
		plain_len = 10;
	} else if (valid_auth && header[1] == 5 && payload[13] == 4 && payload[15] == 0x2c) {
		plain_len = 0;
	}
	assert(wire_len <= *rxlen);
	memset(rx, 0, wire_len);
	if (wire_len >= 2) {
		rx[wire_len - 2] = status >> 8;
		rx[wire_len - 1] = status;
	}
	*rxlen = wire_len;
	return SM_OK;
}
smStatus_t Se05x_API_SCP03_Decrypt(pSe05xSession_t s, size_t cmdlen, uint8_t *data, size_t len,
				   uint8_t *out, size_t *outlen)
{
	(void)s;
	(void)cmdlen;
	(void)data;
	(void)len;
	if (bad_mac) {
		return SM_NOT_OK;
	}
	assert(plain_len + 2 <= *outlen);
	memcpy(out, plain, plain_len);
	out[plain_len] = 0x90;
	out[plain_len + 1] = 0;
	*outlen = plain_len + 2;
	return SM_OK;
}
static void open_channel(void)
{
	uint8_t version[] = {0x41, 7, 7, 2, 0, 0, 0, 0, 0};
	memcpy(plain, version, sizeof(version));
	plain_len = sizeof(version);
	wire_len = 10;
	status = 0x9000;
	bad_mac = fail_transport = false;
	assert(!nxp_device_open());
	plain_len = 0;
}
static void open_pin(void)
{
	uint8_t value[16] = {0x33};
	open_channel();
	valid_auth = true;
	assert(!nxp_device_auth(NXP_PIN_ID, value));
	valid_auth = false;
	const uint8_t prefix[] = {0x10, 8,  0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
				  0x41, 23, 0x80, 4,    0,    0x2c, 18,   0x41, 16};
	assert(payload_len == sizeof(prefix) + 16 && !memcmp(payload, prefix, sizeof(prefix)));
}
int main(void)
{
	uint8_t value[64] = {0x33}, out[64];
	bool exists;
	configured = false;
	assert(nxp_device_open() == -3);
	configured = true;
	no_scp = true;
	assert(nxp_device_open());
	no_scp = false;
	/* Reject truncated and unsupported authenticated VersionInfo. */
	memcpy(plain, (uint8_t[]){0x41, 3, 7, 2, 0}, 5);
	plain_len = 5;
	assert(nxp_device_open());
	memcpy(plain, (uint8_t[]){0x41, 7, 7, 1, 0, 0, 0, 0, 0}, 9);
	plain_len = 9;
	assert(nxp_device_open());
	open_channel();
	assert(!nxp_device_user(NXP_PIN_ID, value, 10));
	const uint8_t user_prefix[] = {0x11, 9, 8,  0,    0, 0,    0,    0,    0, 0,    0, 0x12,
				       2,    0, 10, 0x41, 4, 0x4f, 0x53, 0x4b, 1, 0x42, 16};
	assert(!memcmp(header, (uint8_t[]){0x80, 0x41, 7, 0}, 4));
	assert(payload_len == sizeof(user_prefix) + 16 &&
	       !memcmp(payload, user_prefix, sizeof(user_prefix)));
	assert(!nxp_device_write(NXP_SEED_ID, NXP_PIN_ID, value, 64));
	const uint8_t seed_policy[] = {0x11, 9, 8, 0x4f, 0x53, 0x4b, 1, 0, 0x22, 0, 0};
	assert(!memcmp(payload, seed_policy, sizeof(seed_policy)));
	assert(!memcmp(header, (uint8_t[]){0x80, 1, 6, 0}, 4));
	plain[0] = 0x41;
	plain[1] = 1;
	plain[2] = 2;
	plain_len = 3;
	assert(!nxp_device_exists(NXP_SEED_ID, &exists) && !exists);
	plain[2] = 1;
	assert(!nxp_device_exists(NXP_SEED_ID, &exists) && exists);
	plain[2] = 0;
	assert(nxp_device_exists(NXP_SEED_ID, &exists));
	unsigned rejected_at = transactions;
	assert(nxp_device_exists(NXP_SEED_ID, &exists) && transactions == rejected_at);
	open_channel();
	plain[0] = 0x41;
	plain[1] = 8;
	memset(plain + 2, 0x77, 8);
	plain_len = 10;
	/* CreateSession's response differs from VerifySessionUserID; inject a bad
	 * verify response and ensure it can never authorize a read. */
	assert(nxp_device_auth(NXP_PIN_ID, value));
	unsigned before = transactions;
	assert(nxp_device_read(NXP_SEED_ID, out, 64));
	assert(transactions == before);
	for (size_t n = 0; n < 10; ++n) {
		open_pin();
		wire_len = n;
		assert(nxp_device_read(NXP_SEED_ID, out, 64));
		for (unsigned i = 0; i < 64; ++i) {
			assert(out[i] == 0);
		}
	}
	open_pin();
	bad_mac = true;
	assert(nxp_device_read(NXP_SEED_ID, out, 64));
	before = transactions;
	bad_mac = false;
	assert(nxp_device_read(NXP_SEED_ID, out, 64) && transactions == before);
	open_pin();
	fail_transport = true;
	assert(nxp_device_read(NXP_SEED_ID, out, 64));
	open_pin();
	plain[0] = 0x41;
	plain[1] = 64;
	memcpy(plain + 2, value, 64);
	plain_len = 66;
	assert(!nxp_device_read(NXP_SEED_ID, out, 64) && !memcmp(value, out, 64));
	plain_len = 65;
	assert(nxp_device_read(NXP_SEED_ID, out, 64));
	open_pin();
	assert(nxp_device_delete_all());
	open_channel();
	valid_auth = true;
	assert(!nxp_device_auth(NXP_RESET_ID, value));
	valid_auth = false;
	assert(nxp_device_read(NXP_SEED_ID, out, 64));
	plain_len = 0;
	wire_len = 2;
	assert(!nxp_device_delete_all());
	const uint8_t delete_inner[] = {0x41, 5, 0x80, 4, 0, 0x2a, 0};
	assert(payload_len == 17 && !memcmp(payload + 10, delete_inner, sizeof(delete_inner)));
	nxp_device_close();
	puts("APDU policy encoding, response lengths and fail-closed channel checks passed");
}
