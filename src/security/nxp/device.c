/* SPDX-License-Identifier: MPL-2.0 */
#include "device.h"
#include "host_crypto_psa.h"
#include "se05x_APDU_apis.h"
#include "se05x_scp03.h"
#include "smCom.h"
#include <psa/crypto.h>
#include "phNxpEse_Api.h"

static Se05xSession_t session;
static struct nxp_credentials keys;
static uint8_t user_session[8];
static bool in_user_session, opened;
static uint32_t authenticated_id;

static void u32be(uint8_t *p, uint32_t n)
{
	p[0] = n >> 24;
	p[1] = n >> 16;
	p[2] = n >> 8;
	p[3] = n;
}

/* Commands used here have fixed, short TLVs (at most 64 data bytes). */
static size_t tlv(uint8_t *p, uint8_t tag, const uint8_t *value, size_t length)
{
	p[0] = tag;
	p[1] = length;
	memcpy(p + 2, value, length);
	return length + 2;
}

static size_t object(uint8_t *p, uint32_t id)
{
	p[0] = 0x41;
	p[1] = 4;
	u32be(p + 2, id);
	return 6;
}

static size_t policy(uint8_t *p, uint32_t reader, uint32_t permissions)
{
	uint8_t rule[9] = {8};
	u32be(rule + 1, reader);
	u32be(rule + 5, permissions);
	return tlv(p, 0x11, rule, sizeof(rule));
}

/* A single, fully protected exchange. Never fall back to plaintext, nor accept
 * a bare success status. Any error invalidates the channel before returning. */
static int exchange(uint8_t ins, uint8_t p1, uint8_t p2, const uint8_t *data, size_t length,
		    uint8_t *reply, size_t *reply_len, bool deleting)
{
	uint8_t command[MAX_APDU_BUFFER] = {0}, response[MAX_APDU_BUFFER] = {0};
	tlvHeader_t header = {{0x80, ins, p1, p2}};
	size_t cmd_len = length, response_len = sizeof(response);
	int rc = NXP_ERROR;
	if (!session.scp03_session || length > 110 || !reply_len) {
		goto done;
	}
	if (in_user_session) {
		uint8_t inner[116] = {0x80, ins, p1, p2};
		size_t inner_len = 4;
		if (length) {
			inner[inner_len++] = length;
			memcpy(inner + inner_len, data, length);
			inner_len += length;
		}
		/* Le=0 for commands that return data, or Lc=0 for DeleteAll. */
		if (ins == 0x02 || deleting) {
			inner[inner_len++] = 0;
		}
		cmd_len = tlv(command, 0x10, user_session, sizeof(user_session));
		cmd_len += tlv(command + cmd_len, 0x41, inner, inner_len);
		header = (tlvHeader_t){{0x80, 0x05, 0, 0}};
		nxp_wipe(inner, sizeof(inner));
	} else if (length) {
		memcpy(command, data, length);
	}
	size_t original_len = cmd_len;
	if (Se05x_API_SCP03_Encrypt(&session, &header, command, cmd_len, 0, command, &cmd_len) !=
		    SM_OK ||
	    smComT1oI2C_TransceiveRaw(session.conn_context, command, cmd_len, response,
				      &response_len) != SM_OK ||
	    response_len < 2 || response_len > sizeof(response)) {
		goto done;
	}
	uint16_t sw = ((uint16_t)response[response_len - 2] << 8) | response[response_len - 1];
	if (sw != SM_OK) {
		/* An error cannot authorize data access, even if injected on the bus. */
		rc = sw == 0x6985 ? NXP_PIN_REJECTED : NXP_ERROR;
		goto done;
	}
	if (deleting && response_len == 2) {
		*reply_len = 0;
		rc = NXP_OK;
		session.scp03_session = 0;
		goto done;
	}
	if (response_len < 10) {
		goto done;
	}
	size_t plain_len = sizeof(command);
	if (Se05x_API_SCP03_Decrypt(&session, original_len, response, response_len, command,
				    &plain_len) != SM_OK ||
	    plain_len < 2 || plain_len > sizeof(command) || command[plain_len - 2] != 0x90 ||
	    command[plain_len - 1] != 0 || plain_len - 2 > *reply_len) {
		goto done;
	}
	*reply_len = plain_len - 2;
	if (*reply_len) {
		memcpy(reply, command, *reply_len);
	}
	rc = NXP_OK;
done:
	if (rc) {
		session.scp03_session = 0;
	}
	nxp_wipe(command, sizeof(command));
	nxp_wipe(response, sizeof(response));
	return rc;
}

static int no_reply(uint8_t ins, uint8_t p1, uint8_t p2, uint8_t *data, size_t len)
{
	size_t reply_len = 0;
	int rc = exchange(ins, p1, p2, data, len, NULL, &reply_len, false);
	return rc ? NXP_ERROR : NXP_OK;
}

void nxp_device_close(void)
{
	if (opened) {
		(void)Se05x_API_SessionClose(&session);
		/* The vendor close can return early on EndOfApdu failure. Its transport
		 * uses one global context; release it even after failed session setup. */
		(void)phNxpEse_close(NULL);
	}
	opened = false;
	in_user_session = false;
	authenticated_id = 0;
	nxp_wipe(&session, sizeof(session));
	nxp_wipe(&keys, sizeof(keys));
	nxp_wipe(user_session, sizeof(user_session));
	nxp_crypto_reset();
}

int nxp_device_open(void)
{
	nxp_device_close();
	if (psa_crypto_init() != PSA_SUCCESS) {
		return NXP_ERROR;
	}
	if (!nxp_credentials_load(&keys)) {
		return NXP_CREDENTIALS_MISSING;
	}
	session.pScp03_enc_key = keys.enc;
	session.scp03_enc_key_len = 16;
	session.pScp03_mac_key = keys.mac;
	session.scp03_mac_key_len = 16;
	session.pScp03_dek_key = keys.dek;
	session.scp03_dek_key_len = 16;
	opened = true;
	if (Se05x_API_SessionOpen(&session) != SM_OK || !session.scp03_session) {
		nxp_device_close();
		return NXP_ERROR;
	}
	/* Read authenticated applet version instead of trusting SELECT alone. */
	uint8_t version[16] = {0};
	size_t len = sizeof(version);
	if (exchange(0x04, 0, 0x20, NULL, 0, version, &len, false) || len != 9 ||
	    version[0] != 0x41 || version[1] != len - 2 || version[2] != 7 || version[3] != 2) {
		nxp_device_close();
		return NXP_ERROR;
	}
	return NXP_OK;
}

int nxp_device_exists(uint32_t id, bool *exists)
{
	uint8_t command[6], reply[3];
	size_t len = sizeof(reply);
	object(command, id);
	if (exchange(0x04, 0, 0x27, command, sizeof(command), reply, &len, false) || len != 3 ||
	    reply[0] != 0x41 || reply[1] != 1 || (reply[2] != 1 && reply[2] != 2)) {
		session.scp03_session = 0;
		return NXP_ERROR;
	}
	*exists = reply[2] == 1;
	return NXP_OK;
}

int nxp_device_read(uint32_t id, uint8_t *data, size_t len)
{
	uint8_t command[6], reply[66] = {0};
	size_t received = sizeof(reply);
	int rc = NXP_ERROR;
	if (len > 64 || !data) {
		return NXP_ERROR;
	}
	if (id == NXP_SEED_ID && authenticated_id != NXP_PIN_ID) {
		nxp_wipe(data, len);
		return NXP_ERROR;
	}
	object(command, id);
	if (!exchange(0x02, 0, 0, command, sizeof(command), reply, &received, false) &&
	    received == len + 2 && reply[0] == 0x41 && reply[1] == len) {
		memcpy(data, reply + 2, len);
		rc = NXP_OK;
	}
	if (rc) {
		nxp_wipe(data, len);
		session.scp03_session = 0;
	}
	nxp_wipe(reply, sizeof(reply));
	return rc;
}

int nxp_device_write(uint32_t id, uint32_t reader, const uint8_t *data, size_t len)
{
	uint8_t command[96] = {0};
	if (!data || len > 64 || len == 0) {
		return NXP_ERROR;
	}
	size_t n = policy(command, reader, POLICY_OBJ_ALLOW_READ | POLICY_OBJ_REQUIRE_SM);
	n += object(command + n, id);
	uint8_t length[2] = {0, len};
	n += tlv(command + n, 0x43, length, 2);
	n += tlv(command + n, 0x44, data, len);
	int rc = no_reply(0x01, 0x06, 0, command, n);
	nxp_wipe(command, sizeof(command));
	return rc;
}

int nxp_device_user(uint32_t id, const uint8_t value[16], unsigned attempts)
{
	uint8_t command[48] = {0};
	if (!value || attempts > 255) {
		return NXP_ERROR;
	}
	/* No read/write/delete permissions. Authentication itself remains allowed.
	 * In particular, deleting/replacing PIN cannot bypass its attempt limit. */
	size_t n = policy(command, 0, 0);
	uint8_t limit[2] = {0, attempts};
	n += tlv(command + n, 0x12, limit, 2);
	n += object(command + n, id);
	n += tlv(command + n, 0x42, value, 16);
	int rc = no_reply(0x41, 0x07, 0, command, n);
	nxp_wipe(command, sizeof(command));
	return rc;
}

int nxp_device_auth(uint32_t id, const uint8_t value[16])
{
	uint8_t command[18], reply[10];
	size_t len = sizeof(reply);
	if (in_user_session || !value) {
		return NXP_ERROR;
	}
	object(command, id);
	int rc = exchange(0x04, 0, 0x1b, command, 6, reply, &len, false);
	if (rc) {
		return rc;
	}
	if (len != 10 || reply[0] != 0x41 || reply[1] != 8) {
		session.scp03_session = 0;
		return NXP_ERROR;
	}
	memcpy(user_session, reply + 2, 8);
	in_user_session = true;
	tlv(command, 0x41, value, 16);
	len = 0;
	rc = exchange(0x04, 0, 0x2c, command, sizeof(command), NULL, &len, false);
	if (rc == NXP_OK) {
		authenticated_id = id;
	}
	nxp_wipe(command, sizeof(command));
	return rc;
}

int nxp_device_reset_auth(void)
{
	bool exists;
	if (nxp_device_exists(NXP_RESET_ID, &exists)) {
		return NXP_ERROR;
	}
	if (!exists && nxp_device_user(NXP_RESET_ID, keys.reset, 0)) {
		return NXP_ERROR;
	}
	return nxp_device_auth(NXP_RESET_ID, keys.reset);
}

int nxp_device_delete_all(void)
{
	size_t len = 0;
	if (!in_user_session || authenticated_id != NXP_RESET_ID) {
		return NXP_ERROR;
	}
	return exchange(0x04, 0, 0x2a, NULL, 0, NULL, &len, true);
}
