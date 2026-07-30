#ifndef APP_MESSAGE_H
#define APP_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bindings.h"

bool app_message_submit(AppMessageSource source, AppMessageAction action, uint32_t value,
			const void *data, size_t len, const void *auxiliary, size_t auxiliary_len);
void app_message_reply(AppMessageSource target, bool success, const void *data, size_t len,
		       const void *auxiliary, size_t auxiliary_len);

#endif
