#include "assets.h"

#define A8_DATA(name)                                                                              \
	static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t name##_data[]

#define A8_DESCRIPTOR(name, width, height)                                                         \
	const lv_image_dsc_t oskey_##name = {                                                      \
		.header = {.magic = LV_IMAGE_HEADER_MAGIC,                                         \
			   .cf = LV_COLOR_FORMAT_A8,                                               \
			   .w = width,                                                             \
			   .h = height,                                                            \
			   .stride = width},                                                       \
		.data_size = sizeof(name##_data),                                                  \
		.data = name##_data,                                                               \
	}

A8_DATA(back) = {
#include <oskey_back.a8.inc>
};
A8_DATA(bluetooth) = {
#include <oskey_bluetooth.a8.inc>
};
A8_DATA(camera) = {
#include <oskey_camera.a8.inc>
};
A8_DATA(chevron_right) = {
#include <oskey_chevron_right.a8.inc>
};
A8_DATA(document) = {
#include <oskey_document.a8.inc>
};
A8_DATA(ethereum) = {
#include <oskey_ethereum.a8.inc>
};
A8_DATA(eye) = {
#include <oskey_eye.a8.inc>
};
A8_DATA(eye_off) = {
#include <oskey_eye_off.a8.inc>
};
A8_DATA(failure) = {
#include <oskey_failure.a8.inc>
};
A8_DATA(imu) = {
#include <oskey_imu.a8.inc>
};
A8_DATA(microphone) = {
#include <oskey_microphone.a8.inc>
};
A8_DATA(passkey) = {
#include <oskey_passkey.a8.inc>
};
A8_DATA(refresh) = {
#include <oskey_refresh.a8.inc>
};
A8_DATA(settings) = {
#include <oskey_settings.a8.inc>
};
A8_DATA(shuffle) = {
#include <oskey_shuffle.a8.inc>
};
A8_DATA(success) = {
#include <oskey_success.a8.inc>
};
A8_DATA(trash) = {
#include <oskey_trash.a8.inc>
};
A8_DATA(usb) = {
#include <oskey_usb.a8.inc>
};
A8_DATA(wallet) = {
#include <oskey_wallet.a8.inc>
};
A8_DATA(wallet_logo) = {
#include <oskey_wallet_logo.a8.inc>
};
A8_DATA(warning) = {
#include <oskey_warning.a8.inc>
};
A8_DATA(wifi) = {
#include <oskey_wifi.a8.inc>
};
A8_DATA(wifi_ap) = {
#include <oskey_wifi_ap.a8.inc>
};
A8_DATA(audio) = {
#include <oskey_audio.a8.inc>
};

A8_DESCRIPTOR(back, 24, 24);
A8_DESCRIPTOR(bluetooth, 24, 24);
A8_DESCRIPTOR(camera, 24, 24);
A8_DESCRIPTOR(chevron_right, 24, 24);
A8_DESCRIPTOR(document, 24, 24);
A8_DESCRIPTOR(ethereum, 24, 24);
A8_DESCRIPTOR(eye, 24, 24);
A8_DESCRIPTOR(eye_off, 24, 24);
A8_DESCRIPTOR(failure, 24, 24);
A8_DESCRIPTOR(imu, 24, 24);
A8_DESCRIPTOR(microphone, 24, 24);
A8_DESCRIPTOR(passkey, 24, 24);
A8_DESCRIPTOR(refresh, 24, 24);
A8_DESCRIPTOR(settings, 24, 24);
A8_DESCRIPTOR(shuffle, 24, 24);
A8_DESCRIPTOR(success, 24, 24);
A8_DESCRIPTOR(trash, 24, 24);
A8_DESCRIPTOR(usb, 24, 24);
A8_DESCRIPTOR(wallet, 24, 24);
A8_DESCRIPTOR(wallet_logo, 60, 60);
A8_DESCRIPTOR(warning, 24, 24);
A8_DESCRIPTOR(wifi, 24, 24);
A8_DESCRIPTOR(wifi_ap, 24, 24);
A8_DESCRIPTOR(audio, 24, 24);
