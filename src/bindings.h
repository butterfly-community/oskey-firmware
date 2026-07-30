#ifndef OSKEY_BINDINGS_H
#define OSKEY_BINDINGS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum AppMessageSource {
  AppMessageSource_Uart = 0,
  AppMessageSource_Bluetooth = 1,
  AppMessageSource_Display = 2,
  AppMessageSource_Fido2 = 4,
  AppMessageSource_Confirmation = 5,
} AppMessageSource;

typedef enum AppMessageAction {
  AppMessageAction_External = 0,
  AppMessageAction_Unlock,
  AppMessageAction_InitCustom,
  AppMessageAction_GenerateMnemonic,
  AppMessageAction_Approve,
  AppMessageAction_Reject,
  AppMessageAction_Restart,
  AppMessageAction_ResetStorage,
  AppMessageAction_Fido2Register,
  AppMessageAction_Fido2Sign,
  AppMessageAction_Confirmation,
} AppMessageAction;

enum DisplayAction
#if __STDC_VERSION__ >= 202311L
  : int32_t
#endif // __STDC_VERSION__ >= 202311L
 {
  DisplayAction_Unspecified = 0,
  DisplayAction_Ready = 1,
  DisplayAction_Mnemonic = 2,
  DisplayAction_Sign = 3,
  DisplayAction_Error = 4,
  DisplayAction_Confirm = 5,
};
#if __STDC_VERSION__ >= 202311L
typedef enum DisplayAction DisplayAction;
#else
typedef int32_t DisplayAction;
#endif // __STDC_VERSION__ >= 202311L

enum AppError
#if __STDC_VERSION__ >= 202311L
  : int32_t
#endif // __STDC_VERSION__ >= 202311L
 {
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
#if __STDC_VERSION__ >= 202311L
typedef enum AppError AppError;
#else
typedef int32_t AppError;
#endif // __STDC_VERSION__ >= 202311L

enum ConfirmationKind
#if __STDC_VERSION__ >= 202311L
  : int32_t
#endif // __STDC_VERSION__ >= 202311L
 {
  ConfirmationKind_Unspecified = 0,
  ConfirmationKind_Sign = 1,
  ConfirmationKind_Fido2 = 2,
};
#if __STDC_VERSION__ >= 202311L
typedef enum ConfirmationKind ConfirmationKind;
#else
typedef int32_t ConfirmationKind;
#endif // __STDC_VERSION__ >= 202311L

void app_message_handle_rs(enum AppMessageSource source,
                           enum AppMessageAction action,
                           uint32_t value,
                           const uint8_t *data,
                           uintptr_t len,
                           const uint8_t *auxiliary,
                           uintptr_t auxiliary_len);

#endif  /* OSKEY_BINDINGS_H */
