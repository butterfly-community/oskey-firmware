#include "transport.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bluetooth/bluetooth.h"
#include "bus.h"
#include "uart.h"

LOG_MODULE_REGISTER(app_transport);

K_THREAD_STACK_DEFINE(app_transport_stack, CONFIG_OSKEY_TRANSPORT_STACK_SIZE);
static struct k_thread app_transport_thread_data;

static int send_result(const struct app_transport_result *result)
{
	struct AppSlice slices[CONFIG_OSKEY_BUS_PAYLOAD_COUNT];
	size_t count = app_payload_slices(result->payload, slices, ARRAY_SIZE(slices));

	if (count == 0) {
		return -EINVAL;
	}

	for (size_t i = 0; i < count; i++) {
		int ret = 0;

		switch (result->route.transport) {
		case Transport_Uart:
			app_uart_send(slices[i].data, slices[i].len);
			break;
		case Transport_Bluetooth:
			ret = oskey_bt_send(result->route.session_id, slices[i].data,
					    slices[i].len);
			break;
		default:
			return -EINVAL;
		}

		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static void app_transport_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct app_transport_result result;
		int ret = app_transport_result_get(&result, K_FOREVER);

		if (ret < 0) {
			LOG_ERR("Failed to receive transport result: %d", ret);
			continue;
		}

		ret = send_result(&result);
		if (ret < 0) {
			LOG_ERR("Transport response failed: %d", ret);
		}
		app_payload_release(result.payload);
	}
}

void app_transport_init(void)
{
	k_thread_create(&app_transport_thread_data, app_transport_stack,
			K_THREAD_STACK_SIZEOF(app_transport_stack), app_transport_thread, NULL,
			NULL, NULL, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
}
