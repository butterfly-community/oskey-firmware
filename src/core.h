#ifndef OSKEY_CORE_H
#define OSKEY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bindings.h"

#ifdef CONFIG_OSKEY_RUST
int app_core_init(void);
bool app_core_confirmation_get(uint32_t id, struct AppConfirmation *confirmation);
#else
static inline int app_core_init(void)
{
	return 0;
}

static inline bool app_core_confirmation_get(uint32_t id, struct AppConfirmation *confirmation)
{
	(void)id;
	(void)confirmation;
	return false;
}
#endif

#endif
