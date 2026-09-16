/* SPDX-License-Identifier: MPL-2.0 */
#ifndef OSKEY_NXP_DEVICE_H
#define OSKEY_NXP_DEVICE_H
#include "nxp.h"

#define NXP_PIN_ID    UINT32_C(0x4f534b01)
#define NXP_SEED_ID   UINT32_C(0x4f534b02)
#define NXP_SALT_ID   UINT32_C(0x4f534b03)
#define NXP_RESET_ID  UINT32_C(0x7fff0205)
#define NXP_PIN_LIMIT 10

/* Board provisioning supplies distinct per-device SCP03 and reset credentials.
 * No default transport keys are compiled into this implementation. */
struct nxp_credentials {
	uint8_t enc[16], mac[16], dek[16], reset[16];
};
bool nxp_credentials_load(struct nxp_credentials *keys);
int nxp_device_open(void);
void nxp_device_close(void);
int nxp_device_exists(uint32_t id, bool *exists);
int nxp_device_read(uint32_t id, uint8_t *data, size_t len);
int nxp_device_write(uint32_t id, uint32_t reader, const uint8_t *data, size_t len);
int nxp_device_user(uint32_t id, const uint8_t value[16], unsigned attempts);
int nxp_device_auth(uint32_t id, const uint8_t value[16]);
int nxp_device_delete_all(void);
int nxp_device_reset_auth(void);
#endif
