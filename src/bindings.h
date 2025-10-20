#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

extern const uint16_t STORAGE_ID_SEED;

extern const uint16_t STORAGE_ID_PIN;

bool app_uart_event_rs(uint8_t *data, uintptr_t len);

void app_event_bytes_handle(void);

bool wallet_check_lock(void);

void wallet_lock(void);

bool wallet_set_pin_cache_from_display(const char *pin);

bool wallet_unlock_from_display(const char *pin);

bool wallet_sign_eth_from_trigger(void);

bool wallet_mnemonic_generate_from_display(uintptr_t mnemonic_length,
                                           char *buffer,
                                           uintptr_t len,
                                           const uint8_t *entry,
                                           bool custom_mode);

bool wallet_init_custom_from_display(const char *mnemonic);
