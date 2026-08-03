#include "transport.h"

#include <errno.h>

#include "bluetooth/bluetooth.h"
#include "uart.h"

int app_transport_send(struct TransportRoute route, const uint8_t *data, size_t len)
{
	switch (route.transport) {
	case Transport_Uart:
		return app_uart_send(data, len);
	case Transport_Bluetooth:
		return oskey_bt_send(route.session_id, data, len);
	default:
		return -EINVAL;
	}
}
