/* SPDX-License-Identifier: MPL-2.0 */
#include "device.h"
#include <string.h>
#ifdef OSKEY_NXP_CREDENTIALS_HEADER
#include OSKEY_NXP_CREDENTIALS_HEADER
#endif

bool nxp_credentials_load(struct nxp_credentials *keys)
{
#ifdef OSKEY_NXP_CREDENTIALS
	static const struct nxp_credentials provisioned = OSKEY_NXP_CREDENTIALS;
	memcpy(keys, &provisioned, sizeof(*keys));
	return true;
#else
	memset(keys, 0, sizeof(*keys));
	return false;
#endif
}
