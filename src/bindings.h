#ifndef OSKEY_BINDINGS_H
#define OSKEY_BINDINGS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum AppError {
  AppError_Unspecified = 0,
  AppError_Failed = 1,
  AppError_Busy = 2,
  AppError_Rejected = 3,
  AppError_Locked = 4,
  AppError_NoPendingAction = 5,
  AppError_DisplayRequired = 6,
  AppError_ExternalRequestRequired = 7,
  AppError_TrustedActionRequired = 8,
  AppError_InvalidAction = 9,
  AppError_UnlockFailed = 10,
};
typedef int32_t AppError;

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

enum DisplayAction {
  DisplayAction_Unspecified = 0,
  DisplayAction_Ready = 1,
  DisplayAction_Mnemonic = 2,
  DisplayAction_Sign = 3,
  DisplayAction_Error = 4,
};
typedef int32_t DisplayAction;

void app_message_handle_rs(enum AppMessageSource source,
                           enum AppMessageAction action,
                           uint32_t value,
                           const uint8_t *data,
                           uintptr_t len,
                           const uint8_t *auxiliary,
                           uintptr_t auxiliary_len);

#endif  /* OSKEY_BINDINGS_H */
