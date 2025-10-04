#ifndef APP_H
#define APP_H

#include "wrapper.h"
#include "display/lvgl.h"

struct k_work app_sign_work;

bool app_csrand_get(void *dst, size_t len);
void app_version_get(void *ver, size_t len);
bool app_check_feature(uint8_t *buffer, size_t len);
bool app_check_storage();
int app_device_euid_get(uint8_t *id, size_t len);
bool app_check_status(uint8_t *buffer, size_t len);
void app_sign_work_handler(struct k_work *work);
void app_sign_trigger();

#endif
