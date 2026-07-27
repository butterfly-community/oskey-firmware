#ifndef OSKEY_USB_INIT_H
#define OSKEY_USB_INIT_H

#include <stdint.h>
#include <zephyr/usb/usbd.h>

struct usbd_context *oskey_usbd_setup(usbd_msg_cb_t msg_cb);

#endif /* OSKEY_USB_INIT_H */
