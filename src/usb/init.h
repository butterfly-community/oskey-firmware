#ifndef USBD_H
#define USBD_H

#include <stdint.h>
#include <zephyr/usb/usbd.h>

struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb);
struct usbd_context *usbd_setup_device(usbd_msg_cb_t msg_cb);

#endif /* USBD_H */
