/* SPDX-License-Identifier: MPL-2.0 */
#include "fixtures.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include "camera/qr_scanner.h"
#include "core.h"
#include "media.h"
#include "net/wifi.h"
#include "security/nxp/nxp.h"
#include "storage.h"

struct AppConfirmation showcase_confirmation;
struct app_entropy_snapshot showcase_entropy;
int showcase_nxp_state = NXP_READY;
int showcase_fido_retries = 8;
lv_draw_buf_t *showcase_camera_frame;
const struct storage_ids storage_ids = {.seed = 1, .unlock_failures = 2, .firmware_update = 3};

ZBUS_CHAN_DEFINE(app_audio_command_chan, struct app_audio_command, NULL, NULL,
                ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(app_audio_state_chan, struct app_audio_status, NULL, NULL,
                ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(.state = APP_AUDIO_IDLE, .volume = 60));
ZBUS_CHAN_DEFINE(app_imu_state_chan, enum app_imu_state, NULL, NULL,
                ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(APP_IMU_READY));
ZBUS_CHAN_DEFINE(app_imu_sample_chan, struct app_imu_sample, NULL, NULL,
                ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(app_imu_command_chan, struct app_imu_command, NULL, NULL,
                ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

void showcase_fixture_init(void)
{
    /* A rotation matrix for a gently tilted presentation pose. */
    struct app_imu_sample sample = {
        .valid = true,
        .rotation = {0.813798f, -0.469846f, 0.342020f,
                     0.543838f, 0.823173f, -0.163176f,
                     -0.204874f, 0.318796f, 0.925417f},
    };
    zbus_chan_pub(&app_imu_sample_chan, &sample, K_NO_WAIT);
    showcase_entropy = (struct app_entropy_snapshot){
        .session = 1, .state = APP_ENTROPY_CAPTURING,
        .current = APP_ENTROPY_SOURCE_TOUCH, .progress_permille = 640,
    };
}

bool app_core_confirmation_get(uint32_t id, struct AppConfirmation *out)
{
    if (id != showcase_confirmation.id) return false;
    *out = showcase_confirmation;
    return true;
}
int app_core_submit_local(enum LocalRequestKind kind, uint32_t value, const void *data,
                          size_t len, const void *aux, size_t aux_len, k_timeout_t timeout)
{
    ARG_UNUSED(kind); ARG_UNUSED(value); ARG_UNUSED(data); ARG_UNUSED(len);
    ARG_UNUSED(aux); ARG_UNUSED(aux_len); ARG_UNUSED(timeout);
    return 0;
}
int app_core_submit_confirmation(uint32_t id, enum ConfirmationChoice choice, k_timeout_t timeout)
{
    ARG_UNUSED(id); ARG_UNUSED(choice); ARG_UNUSED(timeout);
    return 0;
}
int app_nxp_state(void) { return showcase_nxp_state; }
int storage_exists(uint16_t id) { ARG_UNUSED(id); return 0; }
int app_fido_pin_retries(void) { return showcase_fido_retries; }
bool oskey_bt_address_privacy_enabled(void) { return true; }
int oskey_bt_address_privacy_set(bool enabled) { return enabled ? 0 : -EPERM; }
int app_wifi_radio_publish(bool station, bool enabled)
{ ARG_UNUSED(station); ARG_UNUSED(enabled); return 0; }
int app_wifi_scan_publish(void) { return 0; }
int app_wifi_forget_network_publish(void) { return 0; }
int app_wifi_save_network_publish(const char *ssid, size_t ssid_len, const char *password,
                                 size_t password_len, enum app_wifi_security security)
{
    ARG_UNUSED(ssid); ARG_UNUSED(ssid_len); ARG_UNUSED(password);
    ARG_UNUSED(password_len); ARG_UNUSED(security); return 0;
}
uint8_t app_entropy_capabilities(void)
{ return APP_ENTROPY_SOURCE_HARDWARE_RNG | APP_ENTROPY_SOURCE_AUXILIARY_MASK; }
int app_entropy_begin(uint8_t words, uint8_t sources, uint32_t *session)
{ ARG_UNUSED(words); ARG_UNUSED(sources); *session = 1; return 0; }
int app_entropy_feed(uint32_t session, enum app_entropy_source source, const void *data,
                     size_t len, uint32_t units)
{
    ARG_UNUSED(session); ARG_UNUSED(source); ARG_UNUSED(data);
    ARG_UNUSED(len); ARG_UNUSED(units); return 0;
}
int app_entropy_retry(uint32_t session) { ARG_UNUSED(session); return 0; }
int app_entropy_skip(uint32_t session) { ARG_UNUSED(session); return 0; }
void app_entropy_cancel(uint32_t session) { ARG_UNUSED(session); }
int app_entropy_snapshot_get(struct app_entropy_snapshot *snapshot)
{ *snapshot = showcase_entropy; return 0; }
int app_entropy_transcript_get(uint32_t session, void *buffer, size_t size, size_t *written)
{ ARG_UNUSED(session); ARG_UNUSED(buffer); ARG_UNUSED(size); *written = 0; return 0; }

int media_mount(void) { return 0; }
int media_unmount(void) { return 0; }
int media_list(const char *path, size_t offset, struct media_entry *entries,
               size_t capacity, bool *has_more)
{
    static const struct media_entry root[] = {
        {.name = "transactions", .directory = true},
        {.name = "signed-message.json", .size = 1234},
        {.name = "wallet-public.json", .size = 768},
        {.name = "firmware-update.bin", .size = 1468006},
        {.name = "readme.txt", .size = 256},
    };
    static const struct media_entry transactions[] = {
        {.name = "eth-transfer.json", .size = 640},
        {.name = "signed-transfer.json", .size = 1096},
    };
    bool subdirectory = strcmp(path, MEDIA_MOUNT_POINT) != 0;
    const struct media_entry *source = subdirectory ? transactions : root;
    size_t count = subdirectory ? ARRAY_SIZE(transactions) : ARRAY_SIZE(root);
    size_t remaining = offset < count ? count - offset : 0;
    size_t copied = MIN(capacity, remaining);
    if (copied > 0) memcpy(entries, source + offset, copied * sizeof(*entries));
    *has_more = copied < remaining;
    return (int)copied;
}

void *shared_multi_heap_aligned_alloc(enum shared_multi_heap_attr attr, size_t align, size_t size)
{
    ARG_UNUSED(attr); ARG_UNUSED(align);
    return lv_malloc(size);
}
int app_qr_scanner_start(uint32_t *session) { *session = 1; return 0; }
int app_qr_scanner_stop(void) { return 0; }
int app_qr_scanner_get_format(struct video_format *format)
{
    if (showcase_camera_frame == NULL) return -EAGAIN;
    *format = (struct video_format){
        .pixelformat = VIDEO_PIX_FMT_RGB565,
        .width = showcase_camera_frame->header.w,
        .height = showcase_camera_frame->header.h,
        .pitch = showcase_camera_frame->header.stride,
        .size = showcase_camera_frame->data_size,
    };
    return 0;
}
int app_qr_scanner_frame_copy(void *out, size_t size, uint32_t *generation)
{
    if (showcase_camera_frame == NULL || size < showcase_camera_frame->data_size)
        return -ENOSPC;
    memcpy(out, showcase_camera_frame->data, showcase_camera_frame->data_size);
    *generation = 1;
    return 0;
}
int app_qr_scanner_result_copy(uint32_t session, struct app_qr_code *code)
{
    ARG_UNUSED(session);
    static const char payload[] = "ethereum:0x9858EfFD232B4033E47d90003D41EC34EcaEda94";
    *code = (struct app_qr_code){.payload_len = sizeof(payload) - 1, .decode_time_ms = 28};
    memcpy(code->payload, payload, sizeof(payload) - 1);
    return 0;
}
