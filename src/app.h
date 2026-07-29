#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool app_csrand_get(void *dst, size_t len);
void app_version_get(void *ver, size_t len);
bool app_check_feature(uint8_t *buffer, size_t len);
bool app_check_storage(void);
void app_get_chip_model(char *buffer, size_t len);
int app_get_eui64(uint8_t *buffer, size_t len);
int app_get_device_id(uint8_t *buffer, size_t len);
void app_storage_reset(void);
void app_restart(void);

#endif
