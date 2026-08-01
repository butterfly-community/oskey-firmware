#include "assets.h"

#define SVG_DESCRIPTOR(name, width, height)                                                        \
	const lv_image_dsc_t oskey_##name = {                                                      \
		.header = {.magic = LV_IMAGE_HEADER_MAGIC, .w = width, .h = height},               \
		.data_size = sizeof(name##_data),                                                  \
		.data = name##_data,                                                               \
	}

static const uint8_t back_data[] = {
#include <oskey_back.svg.inc>
};
static const uint8_t bluetooth_data[] = {
#include <oskey_bluetooth.svg.inc>
};
static const uint8_t chevron_right_data[] = {
#include <oskey_chevron_right.svg.inc>
};
static const uint8_t document_data[] = {
#include <oskey_document.svg.inc>
};
static const uint8_t ethereum_data[] = {
#include <oskey_ethereum.svg.inc>
};
static const uint8_t eye_data[] = {
#include <oskey_eye.svg.inc>
};
static const uint8_t eye_off_data[] = {
#include <oskey_eye_off.svg.inc>
};
static const uint8_t failure_data[] = {
#include <oskey_failure.svg.inc>
};
static const uint8_t passkey_data[] = {
#include <oskey_passkey.svg.inc>
};
static const uint8_t refresh_data[] = {
#include <oskey_refresh.svg.inc>
};
static const uint8_t settings_data[] = {
#include <oskey_settings.svg.inc>
};
static const uint8_t shuffle_data[] = {
#include <oskey_shuffle.svg.inc>
};
static const uint8_t success_data[] = {
#include <oskey_success.svg.inc>
};
static const uint8_t trash_data[] = {
#include <oskey_trash.svg.inc>
};
static const uint8_t usb_data[] = {
#include <oskey_usb.svg.inc>
};
static const uint8_t wallet_data[] = {
#include <oskey_wallet.svg.inc>
};
static const uint8_t warning_data[] = {
#include <oskey_warning.svg.inc>
};
static const uint8_t wifi_data[] = {
#include <oskey_wifi.svg.inc>
};

SVG_DESCRIPTOR(back, 24, 24);
SVG_DESCRIPTOR(bluetooth, 24, 24);
SVG_DESCRIPTOR(chevron_right, 24, 24);
SVG_DESCRIPTOR(document, 24, 24);
SVG_DESCRIPTOR(ethereum, 24, 24);
SVG_DESCRIPTOR(eye, 24, 24);
SVG_DESCRIPTOR(eye_off, 24, 24);
SVG_DESCRIPTOR(failure, 24, 24);
SVG_DESCRIPTOR(passkey, 24, 24);
SVG_DESCRIPTOR(refresh, 24, 24);
SVG_DESCRIPTOR(settings, 24, 24);
SVG_DESCRIPTOR(shuffle, 24, 24);
SVG_DESCRIPTOR(success, 24, 24);
SVG_DESCRIPTOR(trash, 24, 24);
SVG_DESCRIPTOR(usb, 24, 24);
SVG_DESCRIPTOR(wallet, 24, 24);
SVG_DESCRIPTOR(warning, 24, 24);
SVG_DESCRIPTOR(wifi, 24, 24);
