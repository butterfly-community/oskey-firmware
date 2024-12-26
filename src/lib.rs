#![no_std]

mod rs;

pub extern crate alloc;
pub extern crate zephyr;
use rs::wallet::event_parser;
use zephyr::printkln;

extern "C" {
    pub(crate) fn cs_random(dst: *mut u8, len: usize);
    pub(crate) fn app_uart_tx_push_array(data: *const u8, len: usize);
}


#[no_mangle]
extern "C" fn event_bytes_handle(bytes: *mut u8, len: usize) {
    let bytes = unsafe { core::slice::from_raw_parts(bytes, len) };
    let _event = event_parser(bytes);
    return;
}

#[no_mangle]
extern "C" fn rust_main() {
    printkln!("\n\n\n\n\n\n\n\n\n\nHello Rust! \n");
}
