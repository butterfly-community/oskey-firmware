#![no_std]

use zephyr::printkln;

extern crate zephyr;

#[no_mangle]
extern "C" fn rust_main() {
    printkln!("Test Rust Support {}", zephyr::kconfig::CONFIG_BOARD);
}
