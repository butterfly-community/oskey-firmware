#include "bindings.h"

void app_message_handle_rs(AppMessageSource source, AppMessageAction action, uint32_t value,
			   const uint8_t *data, uintptr_t len, const uint8_t *auxiliary,
			   uintptr_t auxiliary_len)
{
	(void)source;
	(void)action;
	(void)value;
	(void)data;
	(void)len;
	(void)auxiliary;
	(void)auxiliary_len;
}
