#![no_std]
mod rs;
pub extern crate alloc;
pub extern crate zephyr;
use crate::rs::wallet::event_parser;

#[allow(unused_doc_comments)]
/// cbindgen:ignore
extern "C" {
    pub(crate) fn app_cs_random(dst: *mut u8, len: usize) -> bool;
    pub(crate) fn app_uart_tx_push_array(data: *const u8, len: usize);
    pub(crate) fn app_version_get(data: *mut u8, len: usize) -> bool;
    pub(crate) fn storage_seed_check() -> bool;
    pub(crate) fn storage_seed_write(data: *const u8, len: usize, phrase_len: usize) -> bool;
    pub(crate) fn storage_seed_read(data: *mut u8, len: usize) -> bool;
}

#[no_mangle]
extern "C" fn event_bytes_handle(bytes: *mut u8, len: usize) {
    let bytes = unsafe { core::slice::from_raw_parts(bytes, len) };
    let _event = event_parser(bytes);
    return;
}
