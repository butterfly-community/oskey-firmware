/*
 * Copyright (c) 2016-2019 Intel Corporation
 * Copyright (c) 2023-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef CONFIG_OSKEY_USB

#include <zephyr/authentication/fido2/fido2.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/logging/log.h>
#include "init.h"
#include "webusb.h"
LOG_MODULE_REGISTER(webusb);
#include "msosv2.h"
/*
 * WebUSB platform capability and WebUSB URL descriptor.
 * See https://wicg.github.io/webusb for reference.
 */

#define WEBUSB_REQ_GET_URL      0x02U
#define WEBUSB_DESC_TYPE_URL    0x03U
#define WEBUSB_URL_PREFIX_HTTPS 0x01U

#define WEBUSB_VENDOR_CODE  0x01U
#define WEBUSB_LANDING_PAGE 0x01U

struct usb_bos_webusb_desc {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_webusb cap;
} __packed;

static const struct usb_bos_webusb_desc bos_cap_webusb = {
	/* WebUSB Platform Capability Descriptor:
	 * https://wicg.github.io/webusb/#webusb-platform-capability-descriptor
	 */
	.platform =
		{
			.bLength = sizeof(struct usb_bos_platform_descriptor) +
				   sizeof(struct usb_bos_capability_webusb),
			.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
			.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
			.bReserved = 0,
			/* WebUSB Platform Capability UUID
			 * 3408b638-09a9-47a0-8bfd-a0768815b665
			 */
			.PlatformCapabilityUUID =
				{
					0x38,
					0xB6,
					0x08,
					0x34,
					0xA9,
					0x09,
					0xA0,
					0x47,
					0x8B,
					0xFD,
					0xA0,
					0x76,
					0x88,
					0x15,
					0xB6,
					0x65,
				},
		},
	.cap = {.bcdVersion = sys_cpu_to_le16(0x0100),
		.bVendorCode = WEBUSB_VENDOR_CODE,
		.iLandingPage = WEBUSB_LANDING_PAGE}};

/* WebUSB URL Descriptor, see https://wicg.github.io/webusb/#webusb-descriptors */
static const uint8_t webusb_origin_url[] = {
	/* bLength, bDescriptorType, bScheme, UTF-8 encoded URL */
	0x0C, WEBUSB_DESC_TYPE_URL, WEBUSB_URL_PREFIX_HTTPS, 'o', 's', 'k', 'e', 'y', '.', 'x', 'y',
	'z'};

static struct net_buf *webusb_to_host_cb(const struct usbd_context *const ctx,
					 const struct usb_setup_packet *const setup)
{
	LOG_INF("Vendor callback to host");

	if (setup->wIndex == WEBUSB_REQ_GET_URL) {
		struct net_buf *buf;
		uint16_t len;
		uint8_t index = USB_GET_DESCRIPTOR_INDEX(setup->wValue);

		if (index != WEBUSB_LANDING_PAGE) {
			return NULL;
		}

		len = MIN(setup->wLength, sizeof(webusb_origin_url));
		buf = usbd_ep_ctrl_data_in_alloc(ctx, len);
		if (buf == NULL) {
			return NULL;
		}

		LOG_INF("Get URL request, index %u", index);
		net_buf_add_mem(buf, &webusb_origin_url, len);

		return buf;
	}

	return NULL;
}

USBD_DESC_BOS_VREQ_DEFINE(bos_vreq_webusb, sizeof(bos_cap_webusb), &bos_cap_webusb,
			  WEBUSB_VENDOR_CODE, webusb_to_host_cb, NULL);

static void msg_cb(struct usbd_context *const usbd_ctx, const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(usbd_ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(usbd_ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(usbd_ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}
}

int init_usb_stack(void)
{
	struct usbd_context *usbd;
	int ret;

	if (IS_ENABLED(CONFIG_OSKEY_FIDO2)) {
		ret = fido2_init();
		if (ret) {
			LOG_ERR("Failed to initialize FIDO2 (%d)", ret);
			return ret;
		}
	}

	usbd = oskey_usbd_setup(msg_cb);
	if (usbd == NULL) {
		LOG_ERR("Failed to setup USB device");
		return -ENODEV;
	}

	ret = usbd_add_descriptor(usbd, &bos_vreq_msosv2);
	if (ret) {
		LOG_ERR("Failed to add MSOSv2 capability descriptor");
		return ret;
	}

	ret = usbd_add_descriptor(usbd, &bos_vreq_webusb);
	if (ret) {
		LOG_ERR("Failed to add WebUSB capability descriptor");
		return ret;
	}

	ret = usbd_init(usbd);
	if (ret) {
		LOG_ERR("Failed to initialize device support");
		return ret;
	}

	if (!usbd_can_detect_vbus(usbd)) {
		ret = usbd_enable(usbd);
		if (ret) {
			LOG_ERR("Failed to enable device support");
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_OSKEY_FIDO2)) {
		ret = fido2_start();
		if (ret) {
			LOG_ERR("Failed to start FIDO2 (%d)", ret);
			return ret;
		}
	}

	return 0;
}

#else

int init_usb_stack(void)
{
	return 0;
}

#endif /* CONFIG_OSKEY_USB */
