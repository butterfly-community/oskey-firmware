#![no_std]

#[cfg(not(target_has_atomic = "ptr"))]
compile_error!("OSKey requires native pointer-width atomic operations");

extern crate alloc;
extern crate zephyr;

#[cfg(target_os = "linux")]
/// cbindgen:ignore
#[no_mangle]
extern "C" fn rust_eh_personality() {
    // The native simulator uses panic=abort and never unwinds.
}

mod rs;
