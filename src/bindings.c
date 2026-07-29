#include "bindings.h"

bool app_uart_event_rs(const uint8_t *data, uintptr_t len)
{
	(void)data;
	(void)len;
	return false;
}

bool app_event_bytes_handle(void)
{
	return false;
}

bool wallet_check_lock(void)
{
	return true;
}

void wallet_lock(void)
{
}

bool wallet_set_pin_cache_from_display(const char *pin)
{
	(void)pin;
	return false;
}

bool wallet_unlock_from_display(const char *pin)
{
	(void)pin;
	return false;
}

bool wallet_sign_eth_from_trigger(void)
{
	return false;
}

bool wallet_mnemonic_generate_from_display(uintptr_t mnemonic_length, char *buffer, uintptr_t len,
					   const uint8_t *entry, bool custom_mode)
{
	(void)mnemonic_length;
	(void)buffer;
	(void)len;
	(void)entry;
	(void)custom_mode;
	return false;
}

bool wallet_init_custom_from_display(const char *mnemonic)
{
	(void)mnemonic;
	return false;
}
