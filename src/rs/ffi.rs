#![allow(clippy::undocumented_unsafe_blocks)]

use alloc::boxed::Box;
use alloc::collections::VecDeque;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;
use core::ffi::{c_char, c_int};
use oskey_action::proto::AppError;
use oskey_action::{
    ConfirmationChoice, ConfirmationDetails, ConfirmationOutcome, CoreEffect, CoreRequest,
    FidoOperation, FidoOutput, FidoRequest, FidoRequestKind, FidoStatus, FrameParser, LocalAction,
    LocalRequest, LocalRequestKind, LocalResult, Transport, TransportRoute, WalletRuntime,
    WalletState,
};
use zeroize::{Zeroize, Zeroizing};

use crate::rs::platform::Platform;

#[repr(C)]
pub struct StorageIds {
    pub seed: u16,
    pub unlock_failures: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AppSlice {
    pub data: *const u8,
    pub len: usize,
}

impl AppSlice {
    fn new(data: &[u8]) -> Self {
        Self {
            data: data.as_ptr(),
            len: data.len(),
        }
    }
}

#[repr(C)]
#[allow(dead_code)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AppCoreCommandKind {
    Protocol,
    Local,
    Fido,
    Confirm,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AppCoreCommandView {
    pub kind: AppCoreCommandKind,
    pub route: TransportRoute,
    pub local_kind: LocalRequestKind,
    pub fido_kind: FidoRequestKind,
    pub choice: ConfirmationChoice,
    pub request_id: u32,
    pub value: u32,
    pub first_len: usize,
    pub fragments: *const AppSlice,
    pub fragment_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AppCoreEffectKind {
    Transport,
    Local,
    Fido,
    ConfirmationRequired,
    ConfirmationCompleted,
    WalletState,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AppCoreEffectView {
    pub kind: AppCoreEffectKind,
    pub route: TransportRoute,
    pub local_action: LocalAction,
    pub error: AppError,
    pub outcome: ConfirmationOutcome,
    pub wallet_state: WalletState,
    pub id: u32,
    pub request_id: u32,
    pub value: u32,
    pub fido_status: FidoStatus,
    pub data: AppSlice,
    pub auxiliary: AppSlice,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AppConfirmationKind {
    EthMessage = 1,
    EthTransaction = 2,
    Fido = 3,
}

#[repr(C)]
pub struct AppConfirmation {
    pub id: u32,
    pub kind: AppConfirmationKind,
    pub operation: FidoOperation,
    pub truncated: bool,
    pub contract_creation: bool,
    pub account_is_text: bool,
    pub chain_id: u64,
    pub nonce: u64,
    pub gas_limit: u64,
    pub message_length: u64,
    pub input_length: u64,
    pub from_len: usize,
    pub preview_len: usize,
    pub gas_price_len: usize,
    pub to_len: usize,
    pub value_len: usize,
    pub selector_len: usize,
    pub input_hash_len: usize,
    pub signing_hash_len: usize,
    pub rp_id_len: usize,
    pub account_len: usize,
    pub from: [u8; 20],
    pub preview: [u8; 256],
    pub gas_price: [u8; 80],
    pub to: [u8; 20],
    pub value: [u8; 80],
    pub selector: [u8; 4],
    pub input_hash: [u8; 32],
    pub signing_hash: [u8; 32],
    pub rp_id: [u8; 128],
    pub account: [u8; 64],
}

impl AppConfirmation {
    fn new(id: u32, kind: AppConfirmationKind) -> Self {
        Self {
            id,
            kind,
            operation: FidoOperation::Register,
            truncated: false,
            contract_creation: false,
            account_is_text: false,
            chain_id: 0,
            nonce: 0,
            gas_limit: 0,
            message_length: 0,
            input_length: 0,
            from_len: 0,
            preview_len: 0,
            gas_price_len: 0,
            to_len: 0,
            value_len: 0,
            selector_len: 0,
            input_hash_len: 0,
            signing_hash_len: 0,
            rp_id_len: 0,
            account_len: 0,
            from: [0; 20],
            preview: [0; 256],
            gas_price: [0; 80],
            to: [0; 20],
            value: [0; 80],
            selector: [0; 4],
            input_hash: [0; 32],
            signing_hash: [0; 32],
            rp_id: [0; 128],
            account: [0; 64],
        }
    }

    fn copy<const N: usize>(source: &[u8], destination: &mut [u8; N]) -> Option<usize> {
        let target = destination.get_mut(..source.len())?;
        target.copy_from_slice(source);
        Some(source.len())
    }

    fn from_details(id: u32, details: &ConfirmationDetails) -> Option<Self> {
        match details {
            ConfirmationDetails::EthMessage(details) => {
                let mut view = Self::new(id, AppConfirmationKind::EthMessage);
                view.truncated = details.truncated;
                view.message_length = details.byte_length;
                view.from_len = Self::copy(&details.from, &mut view.from)?;
                view.preview_len = Self::copy(details.preview.as_bytes(), &mut view.preview)?;
                view.signing_hash_len = Self::copy(&details.signing_hash, &mut view.signing_hash)?;
                Some(view)
            }
            ConfirmationDetails::EthTransaction(details) => {
                let mut view = Self::new(id, AppConfirmationKind::EthTransaction);
                view.contract_creation = details.contract_creation;
                view.chain_id = details.chain_id;
                view.nonce = details.nonce;
                view.gas_limit = details.gas_limit;
                view.input_length = details.input_length;
                view.from_len = Self::copy(&details.from, &mut view.from)?;
                view.gas_price_len = Self::copy(details.gas_price.as_bytes(), &mut view.gas_price)?;
                view.to_len = Self::copy(&details.to, &mut view.to)?;
                view.value_len = Self::copy(details.value.as_bytes(), &mut view.value)?;
                view.selector_len = Self::copy(&details.selector, &mut view.selector)?;
                view.input_hash_len = Self::copy(&details.input_hash, &mut view.input_hash)?;
                view.signing_hash_len = Self::copy(&details.signing_hash, &mut view.signing_hash)?;
                Some(view)
            }
            ConfirmationDetails::Fido(details) => {
                let mut view = Self::new(id, AppConfirmationKind::Fido);
                view.operation = details.operation;
                view.account_is_text = details.account_is_text;
                view.rp_id_len = Self::copy(details.rp_id.as_bytes(), &mut view.rp_id)?;
                view.account_len = Self::copy(&details.account, &mut view.account)?;
                Some(view)
            }
        }
    }
}

#[allow(unused_doc_comments)]
/// cbindgen:ignore
extern "C" {
    pub(crate) static storage_ids: StorageIds;

    pub(crate) fn app_csrand_get(dst: *mut u8, len: usize) -> bool;
    pub(crate) fn app_version_get(data: *mut u8, len: usize);
    pub(crate) fn app_check_feature(data: *mut u8, len: usize) -> bool;
    pub(crate) fn app_check_storage() -> bool;
    pub(crate) fn app_display_ready() -> bool;
    pub(crate) fn app_get_chip_model(buffer: *mut c_char, len: usize);
    pub(crate) fn app_get_eui64(buffer: *mut u8, len: usize) -> c_int;
    pub(crate) fn app_get_device_id(buffer: *mut u8, len: usize) -> c_int;
    pub(crate) fn storage_general_check(id: u16) -> c_int;
    pub(crate) fn storage_general_read(data: *mut u8, len: usize, id: u16) -> c_int;
    pub(crate) fn storage_general_write(data: *const u8, len: usize, id: u16) -> bool;
    pub(crate) fn app_storage_reset() -> bool;
    pub(crate) fn app_restart();
}

pub struct AppCore {
    runtime: WalletRuntime<Platform>,
    effects: VecDeque<CoreEffect>,
    current_effect: Option<CoreEffect>,
    transport_frame: Vec<u8>,
}

impl AppCore {
    fn new() -> Self {
        Self {
            runtime: WalletRuntime::new(Platform),
            effects: VecDeque::new(),
            current_effect: None,
            transport_frame: Vec::new(),
        }
    }

    fn clear_current_effect(&mut self) {
        self.transport_frame.zeroize();
        if let Some(mut effect) = self.current_effect.take() {
            wipe_effect(&mut effect);
        }
    }
}

impl Drop for AppCore {
    fn drop(&mut self) {
        self.clear_current_effect();
        while let Some(mut effect) = self.effects.pop_front() {
            wipe_effect(&mut effect);
        }
    }
}

fn wipe_effect(effect: &mut CoreEffect) {
    match effect {
        CoreEffect::Transport(_, response) => wipe_response(response),
        CoreEffect::Local(result) => result.text.zeroize(),
        CoreEffect::Fido { result, .. } => {
            result.credential_id.zeroize();
            result.data.zeroize();
        }
        _ => {}
    }
}

fn wipe_response(response: &mut oskey_action::proto::ResData) {
    use oskey_action::proto::res_data::Payload;

    let Some(payload) = response.payload.as_mut() else {
        return;
    };
    match payload {
        Payload::ErrorResponse(response) => response.message.zeroize(),
        Payload::Unknown(_) | Payload::WaitForUserActionResponse(_) => {}
        Payload::VersionResponse(response) => {
            response.version.zeroize();
            response.sn.zeroize();
            if let Some(features) = response.features.as_mut() {
                features.support_mask.zeroize();
            }
        }
        Payload::StatusResponse(response) => response.status_mask.zeroize(),
        Payload::InitWalletResponse(response) => response.mnemonic.zeroize(),
        Payload::DerivePublicKeyResponse(response) => {
            response.path.zeroize();
            response.public_key.zeroize();
        }
        Payload::SignResponse(response) => {
            response.message.zeroize();
            response.public_key.zeroize();
            response.pre_hash.zeroize();
            response.signature.zeroize();
        }
    }
}

unsafe fn slice<'a>(data: *const u8, len: usize) -> Option<&'a [u8]> {
    if data.is_null() || len == 0 {
        (len == 0).then_some(&[])
    } else {
        Some(unsafe { core::slice::from_raw_parts(data, len) })
    }
}

unsafe fn slices<'a>(data: *const AppSlice, len: usize) -> Option<&'a [AppSlice]> {
    if data.is_null() || len == 0 {
        (len == 0).then_some(&[])
    } else {
        Some(unsafe { core::slice::from_raw_parts(data, len) })
    }
}

fn payload(slices: &[AppSlice]) -> Option<Zeroizing<Vec<u8>>> {
    let len = slices
        .iter()
        .try_fold(0usize, |len, slice| len.checked_add(slice.len))?;
    let mut payload = Zeroizing::new(Vec::with_capacity(len));

    for fragment in slices {
        payload.extend_from_slice(unsafe { slice(fragment.data, fragment.len) }?);
    }
    Some(payload)
}

fn invalid_local() -> CoreEffect {
    CoreEffect::Local(LocalResult {
        action: LocalAction::Error,
        error: AppError::Failed,
        value: 0,
        text: String::new(),
    })
}

fn invalid_fido(id: u32) -> CoreEffect {
    CoreEffect::Fido {
        id,
        result: FidoOutput {
            status: FidoStatus::Failed,
            credential_id: Vec::new(),
            data: Vec::new(),
        },
    }
}

fn local_request<'a>(
    command: &AppCoreCommandView,
    data: &'a [u8],
    auxiliary: &'a [u8],
) -> Option<LocalRequest<'a>> {
    match command.local_kind {
        LocalRequestKind::Unlock => core::str::from_utf8(data).map(LocalRequest::Unlock).ok(),
        LocalRequestKind::InitCustom => core::str::from_utf8(data).ok().and_then(|words| {
            core::str::from_utf8(auxiliary)
                .map(|pin| LocalRequest::InitCustom { words, pin })
                .ok()
        }),
        LocalRequestKind::GenerateMnemonic => Some(LocalRequest::GenerateMnemonic {
            words: command.value,
            entropy: data,
        }),
        LocalRequestKind::Restart => Some(LocalRequest::Restart),
        LocalRequestKind::ResetStorage => Some(LocalRequest::ResetStorage),
    }
}

fn fido_request<'a>(
    command: &AppCoreCommandView,
    data: &'a [u8],
    auxiliary: &'a [u8],
) -> Option<FidoRequest<'a>> {
    match command.fido_kind {
        FidoRequestKind::Register => core::str::from_utf8(data).ok().and_then(|rp_id| {
            u8::try_from(command.value)
                .ok()
                .map(|cred_protect| FidoRequest::Register {
                    rp_id,
                    cred_protect,
                })
        }),
        FidoRequestKind::Validate if auxiliary.len() == 32 => Some(FidoRequest::Validate {
            credential_id: data,
            rp_id_hash: auxiliary,
        }),
        FidoRequestKind::Sign if auxiliary.len() == 64 => Some(FidoRequest::Sign {
            credential_id: data,
            rp_id_hash: &auxiliary[..32],
            hash: &auxiliary[32..],
        }),
        FidoRequestKind::Confirm => {
            let operation = match command.value {
                value if value == FidoOperation::Register as u32 => FidoOperation::Register,
                value if value == FidoOperation::Authenticate as u32 => FidoOperation::Authenticate,
                value if value == FidoOperation::Select as u32 => FidoOperation::Select,
                value if value == FidoOperation::Authorize as u32 => FidoOperation::Authorize,
                _ => return None,
            };
            Some(FidoRequest::Confirm {
                operation,
                rp_id: data,
                account: auxiliary,
            })
        }
        FidoRequestKind::CancelConfirmation => Some(FidoRequest::CancelConfirmation),
        _ => None,
    }
}

#[no_mangle]
extern "C" fn app_core_create_rs() -> *mut AppCore {
    Box::into_raw(Box::new(AppCore::new()))
}

#[no_mangle]
unsafe extern "C" fn app_core_state_rs(core: *const AppCore) -> WalletState {
    unsafe { core.as_ref() }
        .map(|core| core.runtime.state())
        .unwrap_or(WalletState::Disabled)
}

#[no_mangle]
unsafe extern "C" fn app_core_execute_rs(
    core: *mut AppCore,
    command: *const AppCoreCommandView,
) -> bool {
    let (Some(core), Some(command)) = (unsafe { core.as_mut() }, unsafe { command.as_ref() })
    else {
        return false;
    };
    if core.current_effect.is_some() || !core.effects.is_empty() {
        return false;
    }

    let Some(fragments) = (unsafe { slices(command.fragments, command.fragment_count) }) else {
        return false;
    };
    if fragments
        .iter()
        .any(|fragment| fragment.data.is_null() && fragment.len != 0)
    {
        return false;
    }

    match command.kind {
        AppCoreCommandKind::Protocol => {
            for fragment in fragments {
                let Some(data) = (unsafe { slice(fragment.data, fragment.len) }) else {
                    return false;
                };
                let effects = core.runtime.handle(CoreRequest::Protocol {
                    route: command.route,
                    data,
                });
                core.effects.extend(effects);
            }
        }
        AppCoreCommandKind::Confirm => {
            let effects = core.runtime.handle(CoreRequest::Confirm {
                id: command.request_id,
                choice: command.choice,
            });
            core.effects.extend(effects);
        }
        AppCoreCommandKind::Local | AppCoreCommandKind::Fido => {
            let Some(bytes) = payload(fragments) else {
                return false;
            };
            let Some((data, auxiliary)) = bytes.split_at_checked(command.first_len) else {
                return false;
            };

            let effects = match command.kind {
                AppCoreCommandKind::Local => match local_request(command, data, auxiliary) {
                    Some(request) => core.runtime.handle(CoreRequest::Local(request)),
                    None => vec![invalid_local()],
                },
                AppCoreCommandKind::Fido => match fido_request(command, data, auxiliary) {
                    Some(request) => core.runtime.handle(CoreRequest::Fido {
                        id: command.request_id,
                        request,
                    }),
                    None => vec![invalid_fido(command.request_id)],
                },
                _ => unreachable!(),
            };
            core.effects.extend(effects);
        }
    }
    true
}

#[no_mangle]
unsafe extern "C" fn app_core_effect_next_rs(
    core: *mut AppCore,
    view: *mut AppCoreEffectView,
) -> bool {
    let (Some(core), Some(view)) = (unsafe { core.as_mut() }, unsafe { view.as_mut() }) else {
        return false;
    };

    core.clear_current_effect();
    let Some(effect) = core.effects.pop_front() else {
        return false;
    };
    core.current_effect = Some(effect);

    let mut result = AppCoreEffectView {
        kind: AppCoreEffectKind::WalletState,
        route: TransportRoute {
            transport: Transport::Uart,
            session_id: 0,
        },
        local_action: LocalAction::Ready,
        error: AppError::Unspecified,
        outcome: ConfirmationOutcome::Approved,
        wallet_state: WalletState::Disabled,
        id: 0,
        request_id: 0,
        value: 0,
        fido_status: FidoStatus::Failed,
        data: AppSlice::new(&[]),
        auxiliary: AppSlice::new(&[]),
    };

    match core.current_effect.as_ref().expect("effect was stored") {
        CoreEffect::Transport(route, response) => {
            result.kind = AppCoreEffectKind::Transport;
            result.route = *route;
            core.transport_frame = FrameParser::pack_message(response);
            result.data = AppSlice::new(&core.transport_frame);
        }
        CoreEffect::Local(local) => {
            result.kind = AppCoreEffectKind::Local;
            result.local_action = local.action;
            result.error = local.error;
            result.value = local.value;
            result.data = AppSlice::new(local.text.as_bytes());
        }
        CoreEffect::Fido { id, result: fido } => {
            result.kind = AppCoreEffectKind::Fido;
            result.request_id = *id;
            result.fido_status = fido.status;
            result.data = AppSlice::new(&fido.credential_id);
            result.auxiliary = AppSlice::new(&fido.data);
        }
        CoreEffect::ConfirmationRequired(id) => {
            result.kind = AppCoreEffectKind::ConfirmationRequired;
            result.id = *id;
        }
        CoreEffect::ConfirmationCompleted { id, outcome } => {
            result.kind = AppCoreEffectKind::ConfirmationCompleted;
            result.id = *id;
            result.outcome = *outcome;
        }
        CoreEffect::WalletState(state) => {
            result.kind = AppCoreEffectKind::WalletState;
            result.wallet_state = *state;
        }
    }

    *view = result;
    true
}

#[no_mangle]
unsafe extern "C" fn app_core_confirmation_get_rs(
    core: *const AppCore,
    id: u32,
    result: *mut AppConfirmation,
) -> bool {
    let (Some(core), Some(result)) = (unsafe { core.as_ref() }, unsafe { result.as_mut() }) else {
        return false;
    };

    let Some(details) = core.runtime.confirmation(id) else {
        return false;
    };
    let Some(confirmation) = AppConfirmation::from_details(id, details) else {
        return false;
    };
    *result = confirmation;
    true
}
