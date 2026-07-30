use core::ffi::{c_char, c_int};
use oskey_action::proto::{AppError, DisplayAction};
use oskey_action::{AppMessageAction, AppMessageSource, WalletRuntime};

use crate::rs::platform::Platform;

#[repr(C)]
pub struct StorageIds {
    pub seed: u16,
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
    pub(crate) fn app_display_message(
        action: DisplayAction,
        error: AppError,
        value: u32,
        data: *const u8,
        len: usize,
    );
    pub(crate) fn user_button_request(active: bool);
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
