// SPDX-License-Identifier: MPL-2.0
//! Exercise the production Rust adapter against a controlled C storage boundary.
extern crate alloc;

#[path = "../../rs/ffi.rs"]
mod ffi;
#[path = "../../rs/platform.rs"]
mod platform;
mod rs {
    pub(crate) use crate::{ffi, platform};
}

#[cfg(test)]
mod tests {
    use super::{ffi::StorageIds, platform::Platform};
    use oskey_action::WalletPlatform;
    use std::sync::atomic::{AtomicBool, AtomicI32, AtomicUsize, Ordering::SeqCst};

    static ENABLED: AtomicBool = AtomicBool::new(true);
    static LEGACY: AtomicI32 = AtomicI32::new(0);
    static CHIP: AtomicI32 = AtomicI32::new(0);
    static INITIALIZATIONS: AtomicUsize = AtomicUsize::new(0);

    #[no_mangle]
    static storage_ids: StorageIds = StorageIds {
        seed: 1,
        unlock_failures: 2,
        firmware_update: 3,
    };
    #[no_mangle]
    extern "C" fn app_nxp_enabled() -> bool {
        ENABLED.load(SeqCst)
    }
    #[no_mangle]
    extern "C" fn app_nxp_refresh() -> i32 {
        if CHIP.load(SeqCst) < 0 {
            -1
        } else {
            0
        }
    }
    #[no_mangle]
    extern "C" fn app_nxp_seed_exists() -> i32 {
        CHIP.load(SeqCst)
    }
    #[no_mangle]
    extern "C" fn app_nxp_initialize(_: *const u8, _: *const u8) -> i32 {
        INITIALIZATIONS.fetch_add(1, SeqCst);
        0
    }
    #[no_mangle]
    extern "C" fn app_check_storage() -> bool {
        true
    }
    #[no_mangle]
    extern "C" fn storage_exists(id: u16) -> i32 {
        assert_eq!(id, storage_ids.seed);
        LEGACY.load(SeqCst)
    }

    #[test]
    fn migration_requires_explicit_erase_and_disabled_backend_keeps_legacy_wallet() {
        let platform = Platform;
        for legacy in [-1, 1] {
            LEGACY.store(legacy, SeqCst);
            for chip in [-1, 0, 1] {
                CHIP.store(chip, SeqCst);
                assert!(platform.seed_exists().is_err());
                assert!(!platform.storage_ready());
                assert!(!platform.secure_seed_refresh());
                assert!(platform.secure_seed_initialize(&[1; 32], &[2; 64]).is_err());
            }
        }
        assert_eq!(INITIALIZATIONS.load(SeqCst), 0);
        LEGACY.store(0, SeqCst);
        for chip in [0, 1] {
            CHIP.store(chip, SeqCst);
            assert_eq!(platform.seed_exists().unwrap(), chip == 1);
            assert!(platform.storage_ready() && platform.secure_seed_refresh());
        }
        assert!(platform.secure_seed_initialize(&[1; 32], &[2; 64]).is_ok());
        assert_eq!(INITIALIZATIONS.load(SeqCst), 1);
        ENABLED.store(false, SeqCst);
        CHIP.store(-1, SeqCst);
        LEGACY.store(1, SeqCst);
        assert!(platform.seed_exists().unwrap());
        assert!(platform.storage_ready());
    }
}
