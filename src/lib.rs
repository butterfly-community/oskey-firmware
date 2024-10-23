#![no_std]

extern crate alloc;
extern crate zephyr;

use alloc::vec::Vec;
use zephyr::printkln;

extern "C" {
    fn cs_random(dst: *mut u8, len: usize);
}

/// Crypto safe random rust wrapper
pub fn rust_cs_random_vec(len: usize) -> Vec<u8> {
    let mut buffer = Vec::with_capacity(len);
    unsafe {
        buffer.set_len(len);
        cs_random(buffer.as_mut_ptr(), len);
    }
    buffer
}

#[no_mangle]
extern "C" fn rust_main() {
    printkln!("Hello Rust");
}
