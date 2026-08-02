#ifndef OSKEY_BINDINGS_H
#define OSKEY_BINDINGS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum WalletState {
  WalletState_Disabled,
  WalletState_Setup,
  WalletState_Locked,
  WalletState_Ready,
  WalletState_Busy,
} WalletState;

typedef enum AppCoreCommandKind {
  AppCoreCommandKind_Protocol,
  AppCoreCommandKind_Local,
  AppCoreCommandKind_Fido,
  AppCoreCommandKind_Confirm,
} AppCoreCommandKind;

typedef enum Transport {
  Transport_Uart,
  Transport_Bluetooth,
} Transport;

typedef enum LocalRequestKind {
  LocalRequestKind_Unlock,
  LocalRequestKind_InitCustom,
  LocalRequestKind_GenerateMnemonic,
  LocalRequestKind_Restart,
  LocalRequestKind_ResetStorage,
} LocalRequestKind;

typedef enum FidoRequestKind {
  FidoRequestKind_Register,
  FidoRequestKind_Validate,
  FidoRequestKind_Sign,
  FidoRequestKind_Confirm,
  FidoRequestKind_CancelConfirmation,
} FidoRequestKind;

typedef enum ConfirmationChoice {
  ConfirmationChoice_Approve,
  ConfirmationChoice_Reject,
} ConfirmationChoice;

typedef enum AppCoreEffectKind {
  AppCoreEffectKind_Transport,
  AppCoreEffectKind_Local,
  AppCoreEffectKind_Fido,
  AppCoreEffectKind_ConfirmationRequired,
  AppCoreEffectKind_ConfirmationCompleted,
  AppCoreEffectKind_WalletState,
} AppCoreEffectKind;

typedef enum LocalAction {
  LocalAction_Ready,
  LocalAction_Mnemonic,
  LocalAction_Error,
} LocalAction;

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

typedef enum ConfirmationOutcome {
  ConfirmationOutcome_Approved,
  ConfirmationOutcome_Rejected,
  ConfirmationOutcome_Cancelled,
} ConfirmationOutcome;

typedef enum FidoStatus {
  FidoStatus_Success,
  FidoStatus_Failed,
  FidoStatus_Cancelled,
} FidoStatus;

typedef enum AppConfirmationKind {
  AppConfirmationKind_EthMessage = 1,
  AppConfirmationKind_EthTransaction = 2,
  AppConfirmationKind_Fido = 3,
} AppConfirmationKind;

typedef enum FidoOperation {
  FidoOperation_Register = 1,
  FidoOperation_Authenticate = 2,
  FidoOperation_Select = 3,
  FidoOperation_Authorize = 4,
} FidoOperation;

typedef struct AppCore AppCore;

typedef struct TransportRoute {
  enum Transport transport;
  uint32_t session_id;
} TransportRoute;

typedef struct AppSlice {
  const uint8_t *data;
  uintptr_t len;
} AppSlice;

typedef struct AppCoreCommandView {
  enum AppCoreCommandKind kind;
  struct TransportRoute route;
  enum LocalRequestKind local_kind;
  enum FidoRequestKind fido_kind;
  enum ConfirmationChoice choice;
  uint32_t request_id;
  uint32_t value;
  uintptr_t first_len;
  const struct AppSlice *fragments;
  uintptr_t fragment_count;
} AppCoreCommandView;

typedef struct AppCoreEffectView {
  enum AppCoreEffectKind kind;
  struct TransportRoute route;
  enum LocalAction local_action;
  AppError error;
  enum ConfirmationOutcome outcome;
  enum WalletState wallet_state;
  uint32_t id;
  uint32_t request_id;
  uint32_t value;
  enum FidoStatus fido_status;
  struct AppSlice data;
  struct AppSlice auxiliary;
} AppCoreEffectView;

typedef struct AppConfirmation {
  uint32_t id;
  enum AppConfirmationKind kind;
  enum FidoOperation operation;
  bool truncated;
  bool contract_creation;
  bool account_is_text;
  uint64_t chain_id;
  uint64_t nonce;
  uint64_t gas_limit;
  uint64_t message_length;
  uint64_t input_length;
  uintptr_t from_len;
  uintptr_t preview_len;
  uintptr_t gas_price_len;
  uintptr_t to_len;
  uintptr_t value_len;
  uintptr_t selector_len;
  uintptr_t input_hash_len;
  uintptr_t signing_hash_len;
  uintptr_t rp_id_len;
  uintptr_t account_len;
  uint8_t from[20];
  uint8_t preview[256];
  uint8_t gas_price[80];
  uint8_t to[20];
  uint8_t value[80];
  uint8_t selector[4];
  uint8_t input_hash[32];
  uint8_t signing_hash[32];
  uint8_t rp_id[128];
  uint8_t account[64];
} AppConfirmation;

struct AppCore *app_core_create_rs(void);

enum WalletState app_core_state_rs(const struct AppCore *core);

bool app_core_execute_rs(struct AppCore *core, const struct AppCoreCommandView *command);

bool app_core_effect_next_rs(struct AppCore *core, struct AppCoreEffectView *view);

bool app_core_confirmation_get_rs(const struct AppCore *core,
                                  uint32_t id,
                                  struct AppConfirmation *result);

#endif  /* OSKEY_BINDINGS_H */
