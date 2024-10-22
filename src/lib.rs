#![no_std]

extern crate alloc;
extern crate zephyr;

use alloc::format;
use alloc::{string::String, vec::Vec};
use bip39;
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
    // Get random
    let entropy = rust_cs_random_vec(16);
    // Create Mnemonic
    let mnemonic = bip39::Mnemonic::from_entropy(&entropy).unwrap();
    // Print mnemonic
    let mnemonic_string = mnemonic.words().collect::<Vec<_>>().join(" ");
    printkln!("Mnemonic: {}", mnemonic_string);
    // Mnemonic to seed with null passphrase
    let seed = mnemonic.clone().to_seed("");
    let seed_hex_string: String = seed.iter().map(|&byte| format!("{:02x}", byte)).collect();
    // Print seed
    printkln!("Seed: {}", seed_hex_string);
}
