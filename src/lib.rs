#![no_std]

#[cfg(not(target_has_atomic = "ptr"))]
compile_error!("OSKey requires native pointer-width atomic operations");

extern crate alloc;
extern crate zephyr;

mod rs;
