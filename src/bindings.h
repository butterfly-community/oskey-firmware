#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

extern const uint16_t STORAGE_ID_SEED;

extern const uint16_t STORAGE_ID_PIN;

bool lock_mark_get(void);

void lock_mark_set(bool value);

void lock_mark_lock(void);

void lock_mark_unlock(void);

bool app_version_get_rs(uint8_t *data, uintptr_t len);

bool app_csrand_get_rs(uint8_t *bytes, uintptr_t len);

bool storage_seed_check_rs(void);

int storage_seed_read_rs(uint8_t *data, uintptr_t len);

bool app_uart_event_rs(uint8_t *data, uintptr_t len);

void app_event_bytes_handle(void);

bool wallet_mnemonic_generate_from_display(uintptr_t mnemonic_length, char *buffer, uintptr_t len);

bool wallet_set_pin_cache_from_display(const char *pin);

bool wallet_init_custom_from_display(const char *mnemonic);

bool wallet_unlock_from_display(const char *pin);

bool wallet_sign_eth_from_display(void);
