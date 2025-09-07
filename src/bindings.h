#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void event_bytes_handle(uint8_t *bytes, uintptr_t len);

bool storage_seed_check(void);

bool wallet_init_default_display(uintptr_t mnemonic_length,
                                 const char *password,
                                 char *buffer,
                                 uintptr_t len);

bool wallet_init_custom_display(const char *mnemonic, const char *password);
