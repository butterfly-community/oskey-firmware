#ifndef OSKEY_BINDINGS_H
#define OSKEY_BINDINGS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum AppDisplayAction {
  AppDisplayAction_Ready = 1,
  AppDisplayAction_Mnemonic,
  AppDisplayAction_Sign,
  AppDisplayAction_Error,
} AppDisplayAction;

typedef enum AppMessageAction {
  AppMessageAction_External = 0,
  AppMessageAction_Unlock,
  AppMessageAction_InitCustom,
  AppMessageAction_GenerateMnemonic,
  AppMessageAction_Approve,
  AppMessageAction_Reject,
  AppMessageAction_Restart,
  AppMessageAction_ResetStorage,
} AppMessageAction;

typedef enum AppMessageSource {
  AppMessageSource_Uart = 0,
  AppMessageSource_Bluetooth = 1,
  AppMessageSource_Display = 2,
  AppMessageSource_Button = 3,
} AppMessageSource;

void app_message_handle_rs(enum AppMessageSource source,
                           enum AppMessageAction action,
                           uint32_t value,
                           const uint8_t *data,
                           uintptr_t len,
                           const uint8_t *auxiliary,
                           uintptr_t auxiliary_len);

#endif  /* OSKEY_BINDINGS_H */
