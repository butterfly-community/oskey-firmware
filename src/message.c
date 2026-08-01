#include "message.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bindings.h"
#include "usb/fido2.h"

#define APP_MESSAGE_QUEUE_DEPTH 4

struct app_message {
	AppMessageSource source;
	AppMessageAction action;
	uint16_t len;
	uint16_t auxiliary_len;
	uint32_t value;
	uint8_t data[APP_MESSAGE_DATA_SIZE];
};

K_MEM_SLAB_DEFINE(app_message_slab, sizeof(struct app_message), APP_MESSAGE_QUEUE_DEPTH, 4);

K_MSGQ_DEFINE(app_message_queue, sizeof(struct app_message *), APP_MESSAGE_QUEUE_DEPTH, 4);

LOG_MODULE_REGISTER(app_message);

static void app_message_thread(void *p1, void *p2, void *p3)
{
	struct app_message *message;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (k_msgq_get(&app_message_queue, &message, K_FOREVER) == 0) {
		const uint8_t *auxiliary = &message->data[message->len];

		app_message_handle_rs(message->source, message->action, message->value,
				      message->data, message->len, auxiliary,
				      message->auxiliary_len);
		k_mem_slab_free(&app_message_slab, message);
	}
}

K_THREAD_DEFINE(app_message_thread_id, CONFIG_OSKEY_MESSAGE_STACK_SIZE, app_message_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);

bool app_message_submit(AppMessageSource source, AppMessageAction action, uint32_t value,
			const void *data, size_t len, const void *auxiliary, size_t auxiliary_len)
{
	struct app_message *message;

	if (len > APP_MESSAGE_DATA_SIZE || auxiliary_len > APP_MESSAGE_DATA_SIZE - len ||
	    (len > 0 && data == NULL) || (auxiliary_len > 0 && auxiliary == NULL)) {
		LOG_ERR("Invalid message payload (%zu + %zu)", len, auxiliary_len);
		return false;
	}

	if (k_mem_slab_alloc(&app_message_slab, (void **)&message, K_NO_WAIT) != 0) {
		LOG_ERR("Message pool exhausted");
		return false;
	}

	message->source = source;
	message->action = action;
	message->len = len;
	message->auxiliary_len = auxiliary_len;
	message->value = value;

	if (len > 0) {
		memcpy(message->data, data, len);
	}
	if (auxiliary_len > 0) {
		memcpy(&message->data[len], auxiliary, auxiliary_len);
	}

	if (k_msgq_put(&app_message_queue, &message, K_NO_WAIT) != 0) {
		LOG_ERR("Message queue full");
		k_mem_slab_free(&app_message_slab, message);
		return false;
	}

	return true;
}

void app_fido2_reply(bool success, const void *data, size_t len, const void *auxiliary,
		     size_t auxiliary_len)
{
#ifdef CONFIG_OSKEY_FIDO2
	fido2_message_reply(success, data, len, auxiliary, auxiliary_len);
#else
	ARG_UNUSED(success);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(auxiliary);
	ARG_UNUSED(auxiliary_len);
#endif
}
