use core::ffi::{c_char, c_int};
use oskey_action::proto::{AppError, DisplayAction, FidoOperation};
use oskey_action::{AppMessageAction, AppMessageSource, WalletRuntime};

use crate::rs::platform::Platform;

#[repr(C)]
pub struct StorageIds {
    pub seed: u16,
}

#[repr(C)]
pub struct AppSlice {
    pub data: *const u8,
    pub len: usize,
}

impl AppSlice {
    pub(crate) fn new(data: &[u8]) -> Self {
        Self {
            data: if data.is_empty() {
                core::ptr::null()
            } else {
                data.as_ptr()
            },
            len: data.len(),
        }
    }
}

#[repr(C)]
pub enum AppConfirmationKind {
    EthMessage = 1,
    EthTransaction = 2,
    Fido = 3,
}

#[repr(C)]
pub struct AppConfirmationView {
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
    pub preview: AppSlice,
    pub gas_price: AppSlice,
    pub to: AppSlice,
    pub value: AppSlice,
    pub selector: AppSlice,
    pub input_hash: AppSlice,
    pub signing_hash: AppSlice,
    pub rp_id: AppSlice,
    pub account: AppSlice,
}

impl AppConfirmationView {
    pub(crate) fn new(kind: AppConfirmationKind) -> Self {
        Self {
            kind,
            operation: FidoOperation::Unspecified,
            truncated: false,
            contract_creation: false,
            account_is_text: false,
            chain_id: 0,
            nonce: 0,
            gas_limit: 0,
            message_length: 0,
            input_length: 0,
            preview: AppSlice::new(&[]),
            gas_price: AppSlice::new(&[]),
            to: AppSlice::new(&[]),
            value: AppSlice::new(&[]),
            selector: AppSlice::new(&[]),
            input_hash: AppSlice::new(&[]),
            signing_hash: AppSlice::new(&[]),
            rp_id: AppSlice::new(&[]),
            account: AppSlice::new(&[]),
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
    pub(crate) fn app_get_chip_model(buffer: *mut c_char, len: usize);
    pub(crate) fn app_get_eui64(buffer: *mut u8, len: usize) -> c_int;
    pub(crate) fn app_get_device_id(buffer: *mut u8, len: usize) -> c_int;
    pub(crate) fn storage_general_check(id: u16) -> bool;
    pub(crate) fn storage_general_read(data: *mut u8, len: usize, id: u16) -> c_int;
    pub(crate) fn storage_general_write(data: *const u8, len: usize, id: u16) -> bool;
    pub(crate) fn app_storage_reset();
    pub(crate) fn app_restart();

    pub(crate) fn app_uart_send(data: *const u8, len: usize);
    pub(crate) fn oskey_bt_send(data: *const u8, len: usize) -> c_int;
    pub(crate) fn app_message_reply(
        target: AppMessageSource,
        success: bool,
        data: *const u8,
        len: usize,
        auxiliary: *const u8,
        auxiliary_len: usize,
    );
    pub(crate) fn app_display_message(
        action: DisplayAction,
        error: AppError,
        value: u32,
        data: *const u8,
        len: usize,
    );
    pub(crate) fn app_confirmation_prompt(active: bool, confirmation: *const AppConfirmationView);
    pub(crate) fn app_confirmation_complete(approved: bool);
}

static mut APP_RUNTIME: Option<WalletRuntime<Platform>> = None;

#[no_mangle]
#[allow(static_mut_refs)]
extern "C" fn app_message_handle_rs(
    source: AppMessageSource,
    action: AppMessageAction,
    value: u32,
    data: *const u8,
    len: usize,
    auxiliary: *const u8,
    auxiliary_len: usize,
) {
    let data = if data.is_null() || len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(data, len) }
    };
    let auxiliary = if auxiliary.is_null() || auxiliary_len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(auxiliary, auxiliary_len) }
    };

    // The C message thread is the only caller of the Rust runtime.
    let runtime = unsafe { APP_RUNTIME.get_or_insert_with(|| WalletRuntime::new(Platform)) };

    Platform::output(runtime.message(source, action, value, data, auxiliary));
}
