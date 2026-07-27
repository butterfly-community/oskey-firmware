/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd_config);

#define OSKEY_USB_VID 0x1209

/* By default, do not register the USB DFU class DFU mode instance. */
static const char *const blocklist[] = {
	"dfu_dfu",
	NULL,
};

/*
 * Instantiate the OSKey USB device using the default USB device
 * controller and the pid.codes shared VID. PID 0x20BF is assigned to
 * Butterfly OSKey.
 */
USBD_DEVICE_DEFINE(oskey_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), OSKEY_USB_VID,
		   CONFIG_OSKEY_USBD_PID);

USBD_DESC_LANG_DEFINE(oskey_lang);
USBD_DESC_MANUFACTURER_DEFINE(oskey_manufacturer, CONFIG_OSKEY_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(oskey_product, CONFIG_OSKEY_USBD_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(oskey_serial_number)));

USBD_DESC_CONFIG_DEFINE(fs_config_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_config_desc, "HS Configuration");

static const uint8_t attributes =
	(IS_ENABLED(CONFIG_OSKEY_USBD_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0) |
	(IS_ENABLED(CONFIG_OSKEY_USBD_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0);

/* Full speed configuration */
USBD_CONFIGURATION_DEFINE(oskey_fs_config, attributes, CONFIG_OSKEY_USBD_MAX_POWER,
			  &fs_config_desc);

/* High speed configuration */
USBD_CONFIGURATION_DEFINE(oskey_hs_config, attributes, CONFIG_OSKEY_USBD_MAX_POWER,
			  &hs_config_desc);

#if CONFIG_OSKEY_USBD_20_EXTENSION_DESC
static const struct usb_bos_capability_lpm bos_cap_lpm = {
	.bLength = sizeof(struct usb_bos_capability_lpm),
	.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
	.bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
	.bmAttributes = 0UL,
};

USBD_DESC_BOS_DEFINE(oskey_usb_extension, sizeof(bos_cap_lpm), &bos_cap_lpm);
#endif

static void fix_device_class(struct usbd_context *uds_ctx, const enum usbd_speed speed)
{
	/* Always use class code information from Interface Descriptors */
	if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) || IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) || IS_ENABLED(CONFIG_USBD_MIDI2_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS) || IS_ENABLED(CONFIG_USBD_VIDEO_CLASS)) {
		/*
		 * Class with multiple interfaces have an Interface
		 * Association Descriptor available, use an appropriate triple
		 * to indicate it.
		 */
		usbd_device_set_code_triple(uds_ctx, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	} else {
		usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
	}
}

struct usbd_context *oskey_usbd_setup(usbd_msg_cb_t msg_cb)
{
	int err;

	err = usbd_add_descriptor(&oskey_usbd, &oskey_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&oskey_usbd, &oskey_manufacturer);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&oskey_usbd, &oskey_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	IF_ENABLED(CONFIG_HWINFO, (
		err = usbd_add_descriptor(&oskey_usbd, &oskey_serial_number);
	))
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}
	if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&oskey_usbd) == USBD_SPEED_HS) {
		err = usbd_add_configuration(&oskey_usbd, USBD_SPEED_HS, &oskey_hs_config);
		if (err) {
			LOG_ERR("Failed to add High-Speed configuration");
			return NULL;
		}

		err = usbd_register_all_classes(&oskey_usbd, USBD_SPEED_HS, 1, blocklist);
		if (err) {
			LOG_ERR("Failed to register classes");
			return NULL;
		}

		fix_device_class(&oskey_usbd, USBD_SPEED_HS);
	}

	err = usbd_add_configuration(&oskey_usbd, USBD_SPEED_FS, &oskey_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return NULL;
	}
	err = usbd_register_all_classes(&oskey_usbd, USBD_SPEED_FS, 1, blocklist);
	if (err) {
		LOG_ERR("Failed to register classes");
		return NULL;
	}
	fix_device_class(&oskey_usbd, USBD_SPEED_FS);
	usbd_self_powered(&oskey_usbd, attributes & USB_SCD_SELF_POWERED);

	if (msg_cb != NULL) {
		err = usbd_msg_register_cb(&oskey_usbd, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
	}

#if CONFIG_OSKEY_USBD_20_EXTENSION_DESC
	(void)usbd_device_set_bcd_usb(&oskey_usbd, USBD_SPEED_FS, 0x0201);
	(void)usbd_device_set_bcd_usb(&oskey_usbd, USBD_SPEED_HS, 0x0201);

	err = usbd_add_descriptor(&oskey_usbd, &oskey_usb_extension);
	if (err) {
		LOG_ERR("Failed to add USB 2.0 Extension Descriptor");
		return NULL;
	}
#endif

	return &oskey_usbd;
}
