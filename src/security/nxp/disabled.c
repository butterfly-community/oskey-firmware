/* SPDX-License-Identifier: MPL-2.0 */
#include "nxp.h"
bool app_nxp_enabled(void)
{
	return false;
}
int app_nxp_refresh(void)
{
	return NXP_ERROR;
}
int app_nxp_state(void)
{
	return NXP_UNCONFIGURED;
}
int app_nxp_seed_exists(void)
{
	return NXP_ERROR;
}
int app_nxp_initialize(const uint8_t pin[32], const uint8_t seed[64])
{
	(void)pin;
	(void)seed;
	return NXP_ERROR;
}
int app_nxp_unlock(const uint8_t pin[32], uint8_t seed[64])
{
	(void)pin;
	(void)seed;
	return NXP_ERROR;
}
int app_nxp_erase(void)
{
	return NXP_ERROR;
}
