#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

extern const uint16_t STORAGE_ID_SEED;

extern const uint16_t STORAGE_ID_PIN;

bool app_version_get_rs(uint8_t *data, uintptr_t len);

bool app_csrand_get_rs(uint8_t *bytes, uintptr_t len);

bool storage_seed_check(void);

bool storage_seed_read(uint8_t *data, uintptr_t len);

bool storage_seed_write(const uint8_t *data, uintptr_t len, uintptr_t _phrase_len);

bool app_uart_event_rs(uint8_t *data, uintptr_t len);

void app_event_bytes_handle(void);

bool wallet_init_default_from_display(uintptr_t mnemonic_length,
                                      const char *password,
                                      char *buffer,
                                      uintptr_t len);

bool wallet_init_custom_from_display(const char *mnemonic, const char *password);

bool wallet_sign_eth_from_display(void);
