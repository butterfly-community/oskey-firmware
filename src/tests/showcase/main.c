/* SPDX-License-Identifier: MPL-2.0 */
#include "fixtures.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/display.h>
#include <zephyr/device.h>
#include "assets/assets.h"
#include "camera/qr_scanner.h"
#include "security/nxp/nxp.h"

static const char *output_directory;
static unsigned int captures;
static void settle(void);

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "Showcase: %s\n", message);
        exit(1);
    }
}

static void visible_text(FILE *file, lv_obj_t *object)
{
    if (!lv_obj_is_visible(object)) return;
    if (lv_obj_check_type(object, &lv_label_class)) {
        fprintf(file, "%s\n", lv_label_get_text(object));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        visible_text(file, lv_obj_get_child(object, i));
    }
}

static void capture(const char *name)
{
    settle();
    /* Include modal backdrops from LVGL's top layer in the screen snapshot. */
    lv_obj_t *top = lv_layer_top();
    uint32_t count = lv_obj_get_child_count(top);
    lv_obj_t *overlays[8];
    require(count <= ARRAY_SIZE(overlays), "too many overlay layers");
    for (uint32_t i = 0; i < count; ++i) {
        overlays[i] = lv_obj_get_child(top, 0);
        lv_obj_set_parent(overlays[i], ui.screen);
    }
    lv_obj_update_layout(ui.screen);
    lv_refr_now(NULL);
    lv_draw_buf_t *image = lv_snapshot_take(ui.screen, LV_COLOR_FORMAT_RGB565);
    require(image != NULL, "snapshot allocation failed");
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", output_directory, name);
    FILE *file = fopen(path, "wb");
    require(file != NULL, "cannot create screenshot");
    fprintf(file, "P6\n%u %u\n255\n", image->header.w, image->header.h);
    for (uint32_t y = 0; y < image->header.h; ++y) {
        const uint8_t *row = image->data + y * image->header.stride;
        for (uint32_t x = 0; x < image->header.w; ++x) {
            uint16_t pixel;
            memcpy(&pixel, row + x * 2, sizeof(pixel));
            uint8_t rgb[3] = {
                (uint8_t)(((pixel >> 11) & 31) * 255 / 31),
                (uint8_t)(((pixel >> 5) & 63) * 255 / 63),
                (uint8_t)((pixel & 31) * 255 / 31),
            };
            require(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb), "image write failed");
        }
    }
    require(fclose(file) == 0, "image close failed");
    lv_draw_buf_destroy(image);
    snprintf(path, sizeof(path), "%s/%s.txt", output_directory, name);
    file = fopen(path, "w");
    require(file != NULL, "cannot create visible-text record");
    visible_text(file, ui.screen);
    fclose(file);
    for (uint32_t i = 0; i < count; ++i) lv_obj_set_parent(overlays[i], top);
    captures++;
    printf("Captured %s\n", name);
}

static void page(enum ui_page page, const char *name)
{
    lv_obj_set_style_pad_hor(ui.content, ui.width >= 480 ? 40 : 12, 0);
    ui_open(page);
    capture(name);
}

static lv_obj_t *find_label(lv_obj_t *object, const char *text)
{
    if (lv_obj_check_type(object, &lv_label_class) &&
        strcmp(lv_label_get_text(object), text) == 0) return object;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
        lv_obj_t *found = find_label(lv_obj_get_child(object, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}

static void click_row(const char *text)
{
    lv_obj_t *label = find_label(ui.content, text);
    require(label != NULL, "requested row not found");
    lv_obj_t *row = label;
    while (row != ui.content && !lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE))
        row = lv_obj_get_parent(row);
    require(row != ui.content, "requested row is not clickable");
    lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(ui.screen);
}

static void settle(void)
{
    for (int i = 0; i < 15; ++i) {
        k_msleep(20);
        lv_timer_handler();
    }
}

static void fixture_page(const char *title)
{
    lv_obj_set_style_pad_hor(ui.content, ui.width >= 480 ? 40 : 12, 0);
    ui_open(UI_PAGE_HOME);
    ui_page_begin(title, UI_NAVIGATION_BACK);
    ui.page = UI_PAGE_NONE;
}

static void row(const void *icon, const char *title, const char *detail)
{
    ui_list_row(ui.content, icon, title, detail, NULL, UI_TONE_DEFAULT, NULL, NULL);
}

static lv_obj_t *qr(const char *payload)
{
    lv_obj_t *code = lv_qrcode_create(ui.content);
    lv_qrcode_set_size(code, 264);
    lv_qrcode_set_quiet_zone(code, true);
    lv_qrcode_set_dark_color(code, lv_color_black());
    lv_qrcode_set_light_color(code, lv_color_white());
    lv_obj_set_style_border_color(code, lv_color_white(), 0);
    lv_obj_set_style_border_width(code, 12, 0);
    lv_obj_set_style_align(code, LV_ALIGN_CENTER, 0);
    require(lv_qrcode_update(code, payload, strlen(payload)) == LV_RESULT_OK,
            "QR encoding failed");
    return code;
}

static void mnemonic(uint8_t words, const char *last, const char *name)
{
    ui.mnemonic_words = words;
    ui.mnemonic[0] = '\0';
    for (uint8_t i = 1; i < words; ++i) strcat(ui.mnemonic, "abandon ");
    strcat(ui.mnemonic, last);
    page(UI_PAGE_MNEMONIC, name);
}

static void confirmation_base(enum AppConfirmationKind kind)
{
    showcase_confirmation = (struct AppConfirmation){
        .id = 7, .kind = kind, .chain_id = 1, .nonce = 3, .gas_limit = 21000,
        .from_len = 20, .to_len = 20, .signing_hash_len = 32,
    };
    static const uint8_t from[20] = {
        0x98, 0x58, 0xef, 0xfd, 0x23, 0x2b, 0x40, 0x33, 0xe4, 0x7d,
        0x90, 0x00, 0x3d, 0x41, 0xec, 0x34, 0xec, 0xae, 0xda, 0x94,
    };
    memcpy(showcase_confirmation.from, from, sizeof(from));
    memset(showcase_confirmation.to, 0x22, 20);
    for (size_t i = 0; i < 32; ++i) showcase_confirmation.signing_hash[i] = (uint8_t)(i * 7 + 11);
#define COPY_TEXT(member, value) do { \
    memcpy(showcase_confirmation.member, value, sizeof(value) - 1); \
    showcase_confirmation.member##_len = sizeof(value) - 1; \
} while (0)
    COPY_TEXT(path, "m/44'/60'/0'/0/0");
    COPY_TEXT(value, "0.025 ETH");
    COPY_TEXT(gas_price, "20 Gwei");
    COPY_TEXT(preview, "Hello from OSKey. My keys, my choices.");
    showcase_confirmation.message_length = showcase_confirmation.preview_len;
}

static void prepared(void)
{
    showcase_confirmation.prepared = true;
    showcase_confirmation.public_key_len = 65;
    showcase_confirmation.signature_len = 64;
    showcase_confirmation.public_key[0] = 4;
    for (size_t i = 1; i < 65; ++i) showcase_confirmation.public_key[i] = (uint8_t)(i * 3 + 19);
    for (size_t i = 0; i < 64; ++i) showcase_confirmation.signature[i] = (uint8_t)(i * 5 + 23);
}

static void review(const char *name)
{
    lv_obj_set_style_pad_hor(ui.content, ui.width >= 480 ? 40 : 12, 0);
    ui.confirmation_id = showcase_confirmation.id;
    ui_open(UI_PAGE_CONFIRMATION);
    ui_dialog_close();
    capture(name);
}

static void fido(enum FidoOperation operation)
{
    confirmation_base(AppConfirmationKind_Fido);
    showcase_confirmation.operation = operation;
    showcase_confirmation.account_is_text = true;
    COPY_TEXT(rp_id, "www.google.com");
    COPY_TEXT(account, "demo@example.com");
}

static void wallet_scenes(void)
{
    page(UI_PAGE_SPLASH, "01-startup");
    page(UI_PAGE_CAPABILITIES, "02-capabilities");
    page(UI_PAGE_PIN_NEW, "03-create-pin");
    lv_textarea_set_text(ui.input, "OSKey123!");
    capture("04-pin-entered");
    page(UI_PAGE_PIN_CONFIRM, "05-confirm-pin");
    page(UI_PAGE_SOURCE, "06-recovery-source");
    page(UI_PAGE_LENGTH, "07-mnemonic-length");
    ui.mnemonic_words = 24;
    page(UI_PAGE_ENTROPY_METHOD, "08-entropy-method");
    ui.entropy_sources = APP_ENTROPY_SOURCE_AUXILIARY_MASK;
    page(UI_PAGE_ENTROPY_SOURCES, "09-entropy-sources");
    ui.entropy_session = 1;
    page(UI_PAGE_ENTROPY_COLLECT, "10-entropy-touch");
    showcase_entropy.current = APP_ENTROPY_SOURCE_IMU;
    showcase_entropy.completed = APP_ENTROPY_SOURCE_TOUCH;
    page(UI_PAGE_ENTROPY_COLLECT, "11-entropy-motion");
    showcase_entropy.current = APP_ENTROPY_SOURCE_CAMERA;
    showcase_entropy.completed |= APP_ENTROPY_SOURCE_IMU;
    page(UI_PAGE_ENTROPY_COLLECT, "12-entropy-camera");
    showcase_entropy.current = APP_ENTROPY_SOURCE_MICROPHONE;
    showcase_entropy.completed |= APP_ENTROPY_SOURCE_CAMERA;
    page(UI_PAGE_ENTROPY_COLLECT, "13-entropy-microphone");
    showcase_entropy.state = APP_ENTROPY_READY;
    showcase_entropy.completed = APP_ENTROPY_SOURCE_AUXILIARY_MASK;
    showcase_entropy.current = APP_ENTROPY_SOURCE_NONE;
    page(UI_PAGE_ENTROPY_COLLECT, "14-entropy-ready");
    ui.entropy_bits = 128;
    memset(ui.entropy, 0x69, sizeof(ui.entropy));
    page(UI_PAGE_ENTROPY, "15-custom-entropy");
    lv_obj_scroll_to_y(ui.content, 400, LV_ANIM_OFF);
    capture("84-custom-entropy-generate");
    mnemonic(12, "about", "16-mnemonic-12");
    mnemonic(18, "agent", "17-mnemonic-18");
    mnemonic(24, "art", "18-mnemonic-24");
    page(UI_PAGE_VERIFY, "19-verify-mnemonic");
    page(UI_PAGE_IMPORT, "20-import-wallet");
    page(UI_PAGE_PASSPHRASE, "21-passphrase");
    page(UI_PAGE_PASSPHRASE_CONFIRM, "22-confirm-passphrase");
    ui.status.wallet = WalletState_Ready;
    page(UI_PAGE_HOME, "23-wallet-home");
    ui.status.wallet = WalletState_Locked;
    page(UI_PAGE_LOCKED, "24-unlock-wallet");
    lv_textarea_set_text(ui.input, "OSKey123!");
    lv_obj_send_event(ui.input, LV_EVENT_CLICKED, NULL);
    settle();
    capture("25-unlock-keyboard");
    ui.status.wallet = WalletState_Ready;
    fixture_page("Ethereum account");
    ui_section(ui.content, "HD WALLET");
    row(&oskey_ethereum, "Ethereum", "Chain ID 1 / account 0");
    row(&oskey_document, "Derivation path", "m/44'/60'/0'/0/0");
    row(&oskey_wallet, "Public address", "0x9858EfFD232B4033E47d90003D41EC34EcaEda94");
    qr("ethereum:0x9858EfFD232B4033E47d90003D41EC34EcaEda94");
    capture("26-hd-account");
}

static void signing_scenes(void)
{
    confirmation_base(AppConfirmationKind_EthMessage);
    ui.confirmation_id = 7;
    ui_open(UI_PAGE_CONFIRMATION);
    capture("27-private-key-confirmation");
    ui_dialog_close();
    capture("28-ethereum-message");
    click_row("Technical details");
    capture("29-message-details");
    prepared();
    review("30-message-signature");
    click_row("Technical details");
    lv_obj_scroll_to_y(ui.content, 200, LV_ANIM_OFF);
    capture("31-message-signature-details");
    confirmation_base(AppConfirmationKind_EthTransaction);
    review("32-ethereum-transaction");
    click_row("Technical details");
    lv_obj_scroll_to_y(ui.content, 170, LV_ANIM_OFF);
    capture("33-transaction-details");
    prepared();
    review("34-transaction-signature");
    confirmation_base(AppConfirmationKind_EthTransaction);
    showcase_confirmation.contract_creation = true;
    showcase_confirmation.input_length = 512;
    review("35-contract-creation");

    fixture_page("Air-gapped signing");
    row(&oskey_camera, "1. Scan signing request", "Exchange the transaction through QR codes");
    qr("{\"type\":\"eth-sign-request\",\"chainId\":1,\"to\":\"0x2222222222222222222222222222222222222222\",\"value\":\"0.025 ETH\"}");
    row(&oskey_ethereum, "Review on OSKey", "Confirm recipient, network and value");
    capture("36-airgap-request");
    confirmation_base(AppConfirmationKind_EthTransaction);
    review("37-airgap-review");
    fixture_page("Signature QR code");
    row(&oskey_success, "3. Return the signature", "Scan this result with your wallet application");
    qr("{\"type\":\"eth-signature\",\"signature\":\"0x171c21262b30353a3f44494e53585d62676c71767b80858a8f94999ea3a8adb2b7bcc1c6cbd0d5dadfe4e9eef3f8fd02070c11161b20252a2f34393e43484d52\"}");
    row(&oskey_document, "Request reviewed", "Ethereum / Chain ID 1 / 0.025 ETH");
    capture("38-airgap-signature");

    fido(FidoOperation_Register);
    ui.confirmation_id = 7;
    ui_open(UI_PAGE_CONFIRMATION);
    capture("39-google-passkey-permission");
    ui_dialog_close();
    capture("40-google-create-passkey");
    prepared();
    showcase_confirmation.credential_id_len = 32;
    memset(showcase_confirmation.credential_id, 0x3a, 32);
    review("41-google-passkey-result");
    click_row("Technical details");
    capture("85-google-credential-details");
    fido(FidoOperation_Authenticate);
    review("42-google-authenticate");
    prepared();
    review("43-google-signature");
    click_row("Technical details");
    capture("44-google-signature-details");
    fido(FidoOperation_Select);
    review("45-google-presence");
    fido(FidoOperation_Authorize);
    COPY_TEXT(account, "Create and use passkeys");
    review("46-google-authorize");
    page(UI_PAGE_FIDO_PIN_RECOVER, "47-fido-pin-recovery");
}

static void hardware_scenes(void)
{
    page(UI_PAGE_SETTINGS, "48-device-settings");
    showcase_nxp_state = NXP_EMPTY;
    ui.status.wallet = WalletState_Setup;
    page(UI_PAGE_NXP, "49-nxp-initialize");
    showcase_nxp_state = NXP_READY;
    ui.status.wallet = WalletState_Ready;
    page(UI_PAGE_NXP, "50-nxp-unlocked");
    ui.status.wallet = WalletState_Locked;
    page(UI_PAGE_NXP, "51-nxp-locked");
    showcase_nxp_state = NXP_AUTH_REJECTED;
    page(UI_PAGE_NXP, "52-nxp-pin-protection");
    ui_dialog_show(&oskey_trash, "Erase wallet?", "Clear the seed and initialize a new wallet.",
                   "Erase wallet", UI_TONE_DANGER, NULL);
    capture("53-nxp-erase");
    ui.status.wallet = WalletState_Ready;
    page(UI_PAGE_WIFI, "54-wifi-connected");
    lv_obj_scroll_to_y(ui.content, 420, LV_ANIM_OFF);
    capture("55-wifi-networks");
    snprintf(ui.wifi_ssid, sizeof(ui.wifi_ssid), "OSKey-Studio");
    ui.wifi_security = APP_WIFI_SECURITY_PERSONAL;
    page(UI_PAGE_WIFI_PASSWORD, "56-wifi-password");
    fixture_page("Wi-Fi provisioning");
    row(&oskey_wifi_ap, "OSKey-AP", "Temporary setup access point / WPA2");
    row(&oskey_document, "Provisioning portal", "http://192.168.4.1");
    qr("WIFI:T:WPA;S:OSKey-AP;P:12345678;;");
    row(&oskey_wifi, "Saved network", "OSKey-Studio / starts on restart");
    capture("57-wifi-access-point");
    ui.status.bluetooth = APP_BLUETOOTH_ADVERTISING;
    page(UI_PAGE_BLUETOOTH, "58-bluetooth-discovery");
    ui.status.bluetooth = APP_BLUETOOTH_CONNECTED;
    page(UI_PAGE_BLUETOOTH, "59-bluetooth-connected");
    page(UI_PAGE_USB, "60-usb-interfaces");
    showcase_fido_retries = 0;
    page(UI_PAGE_USB, "61-fido-pin-protection");
    showcase_fido_retries = 8;
    page(UI_PAGE_AUDIO, "62-audio-volume");
    ui.status.audio.state = APP_AUDIO_PLAYING;
    ui.status.audio.volume = 80;
    ui_status_update(&ui.status);
    page(UI_PAGE_AUDIO, "63-audio-playback");
    fixture_page("Microphone");
    row(&oskey_microphone, "Ambient sound", "Microphone enabled / 16 kHz mono");
    ui_section(ui.content, "INPUT LEVEL");
    lv_obj_t *level = lv_bar_create(ui.content);
    lv_obj_set_size(level, LV_PCT(100), 20);
    lv_bar_set_value(level, 68, LV_ANIM_OFF);
    row(&oskey_audio, "Live level", "68% / presentation audio sample");
    row(&oskey_shuffle, "Entropy source", "Mix ambient sound with hardware randomness");
    capture("64-microphone");
    ui_open(UI_PAGE_IMU);
    settle();
    capture("65-imu-orientation");
    struct app_imu_sample pose = {.valid = true,
        .rotation = {0.866025f, 0, -0.5f, 0, 1, 0, 0.5f, 0, 0.866025f}};
    zbus_chan_pub(&app_imu_sample_chan, &pose, K_NO_WAIT);
    settle();
    capture("66-imu-tilt");
    page(UI_PAGE_CAMERA, "67-camera");

    fixture_page("Camera preview");
    lv_obj_t *code = qr("ethereum:0x9858EfFD232B4033E47d90003D41EC34EcaEda94");
    lv_obj_update_layout(ui.screen);
    showcase_camera_frame = lv_snapshot_take(code, LV_COLOR_FORMAT_RGB565);
    require(showcase_camera_frame != NULL, "camera frame allocation failed");
    ui_open(UI_PAGE_QR_SCANNER);
    struct app_qr_scanner_event event = {.state = APP_QR_SCANNER_RUNNING, .session = 1};
    ui_qr_event(&event);
    settle();
    capture("68-qr-scanner");
    event.state = APP_QR_SCANNER_RESULT;
    ui_qr_event(&event);
    capture("69-qr-decoded");
    page(UI_PAGE_FILES, "70-sd-files");
    click_row("transactions");
    capture("71-sd-directory");

    fixture_page("MQTT");
    row(&oskey_wifi, "Connected", "Network: OSKey-Studio / MQTT over TCP");
    row(&oskey_document, "Broker", "mqtt.example.com:1883");
    row(&oskey_document, "Publish", "oskey/demo/status");
    row(&oskey_document, "Subscribe", "oskey/demo/events");
    row(&oskey_success, "Last event", "Device ready / presentation message");
    capture("72-mqtt");
    fixture_page("Firmware update");
    row(&oskey_refresh, "MCUboot", "Signed firmware / version protection");
    row(&oskey_bluetooth, "Update transport", "Authenticated Bluetooth or UART update mode");
    row(&oskey_document, "Selected image", "OSKey 0.4.0 / 1.4 MiB");
    row(&oskey_success, "Signature", "EC P-256 / verified");
    capture("73-firmware-update");
    lv_obj_t *progress = lv_bar_create(ui.content);
    lv_obj_set_size(progress, LV_PCT(100), 20);
    lv_bar_set_value(progress, 72, LV_ANIM_OFF);
    row(&oskey_refresh, "Transfer progress", "72% / writing the secondary image slot");
    capture("74-update-progress");
    fixture_page("Firmware ready");
    row(&oskey_success, "Update ready", "Image signature and version verified");
    row(&oskey_refresh, "Restart to apply", "MCUboot selects the signed image");
    row(&oskey_wallet, "Wallet preserved", "Seed and settings retained through the update");
    capture("75-update-ready");
    page(UI_PAGE_HOME, "76-device-maintenance");
    ui_dialog_show(&oskey_refresh, "Restart OSKey?", "Restart the device with saved settings.",
                   "Restart", UI_TONE_ACTIVE, NULL);
    capture("77-restart");
    ui_dialog_close();
    ui_dialog_show(&oskey_trash, "Erase device data?",
                   "Clear wallet, passkeys and network settings.", "Erase data", UI_TONE_DANGER, NULL);
    capture("78-reset");
    page(UI_PAGE_STORAGE_ERROR, "79-storage-recovery");
}

static void composition_scenes(void)
{
    fixture_page("Build your OSKey");
    ui_section(ui.content, "COMBINE MODULES");
    static const struct { const void *icon; const char *title; const char *detail; } modules[] = {
        {&oskey_wallet, "Wallet and signing", "Mnemonic / HD wallet / Ethereum"},
        {&oskey_passkey, "FIDO2", "Passkeys and on-device confirmation"},
        {&oskey_wallet, "NXP A5000", "Optional secure seed storage"},
        {&oskey_document, "Display and touch", "Review, confirm and interact"},
        {&oskey_wifi, "Wi-Fi / Bluetooth / USB", "Choose the connections you use"},
        {&oskey_camera, "Camera and QR", "Scanning and air-gapped signing"},
        {&oskey_audio, "Audio and microphone", "Sound feedback and ambient input"},
        {&oskey_imu, "IMU", "Motion sensing and orientation"},
        {&oskey_document, "SD card", "Read-only file browsing"},
        {&oskey_refresh, "Signed updates", "MCUboot / Bluetooth / UART"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(modules); ++i)
        ui_list_row(ui.content, modules[i].icon, modules[i].title, modules[i].detail,
                    "On", UI_TONE_ACTIVE, NULL, NULL);
    capture("80-module-composition");
    fixture_page("Software wallet");
    row(&oskey_wallet, "Seed storage", "Encrypted seed stored on the device");
    row(&oskey_passkey, "Wallet PIN", "Unlock the wallet and clear the seed on lock");
    row(&oskey_document, "Build profile", "Software storage selected at compile time");
    row(&oskey_success, "Your configuration", "Choose the wallet backend for your hardware");
    capture("81-software-wallet");
    confirmation_base(AppConfirmationKind_EthTransaction);
    showcase_confirmation.input_length = 68;
    showcase_confirmation.selector_len = 4;
    memcpy(showcase_confirmation.selector, (uint8_t[]){0xa9, 0x05, 0x9c, 0xbb}, 4);
    showcase_confirmation.input_hash_len = 32;
    memset(showcase_confirmation.input_hash, 0x4e, 32);
    showcase_confirmation.gas_limit = 65000;
    review("82-contract-call");
    click_row("Technical details");
    lv_obj_scroll_to_y(ui.content, 200, LV_ANIM_OFF);
    capture("83-contract-call-details");
}

int main(void)
{
    output_directory = getenv("OSKEY_SHOWCASE_OUTPUT");
    require(output_directory != NULL, "set OSKEY_SHOWCASE_OUTPUT to an existing directory");
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    require(device_is_ready(display), "display initialization failed");
    int blanking = display_blanking_off(display);
    require(blanking == 0 || blanking == -ENOSYS, "display activation failed");
    uint8_t features[APP_FEATURE_COUNT];
    memset(features, 1, sizeof(features));
    struct ui_status status = {
        .wifi = {.sta = APP_WIFI_STA_CONNECTED, .ap = APP_WIFI_AP_ACTIVE,
                 .connected_ssid = "OSKey-Studio", .ap_client_connected = true,
                 .dhcp = {.address = "192.168.1.42", .netmask = "255.255.255.0",
                          .gateway = "192.168.1.1", .lease_seconds = 86400}},
        .public_ip = {.address = "203.0.113.42"},
        .bluetooth = APP_BLUETOOTH_CONNECTED, .usb = APP_USB_CONFIGURED,
        .storage = APP_STORAGE_READY, .camera = APP_CAMERA_READY,
        .audio = {.state = APP_AUDIO_IDLE, .volume = 60, .microphone_enabled = true},
        .imu = APP_IMU_READY, .wallet = WalletState_Setup,
    };
    struct app_wifi_config config = {
        .sta_enabled = true, .ap_enabled = true, .saved_ssid = "OSKey-Studio",
    };
    struct app_wifi_scan scan = {.state = APP_WIFI_SCAN_READY, .count = 3,
        .networks = {{.ssid = "OSKey-Studio", .security = APP_WIFI_SECURITY_PERSONAL, .rssi = -42},
                     {.ssid = "Workshop", .security = APP_WIFI_SECURITY_SAE, .rssi = -58},
                     {.ssid = "Guest", .security = APP_WIFI_SECURITY_OPEN, .rssi = -62}}};
    showcase_fixture_init();
    ui_init(features, &status, &config, &scan);
    ui_status_init(&status);
    wallet_scenes();
    signing_scenes();
    hardware_scenes();
    composition_scenes();
    printf("Showcase complete: %u frames\n", captures);
    fflush(stdout);
    exit(0);
}
