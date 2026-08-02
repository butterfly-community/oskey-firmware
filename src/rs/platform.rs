// Platform calls pass live Rust buffers to C only for the duration of each call.
#![allow(clippy::undocumented_unsafe_blocks)]

use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;
use anyhow::{anyhow, Result};
use core::ffi::{c_char, CStr};
use core::fmt::Write;
use core::mem::size_of_val;
use oskey_action::WalletPlatform;

use crate::rs::ffi::{
    app_check_feature, app_check_storage, app_csrand_get, app_display_ready, app_get_chip_model,
    app_get_device_id, app_get_eui64, app_restart, app_storage_reset, app_version_get,
    storage_general_check, storage_general_read, storage_general_write, storage_ids,
};

pub(crate) struct Platform;

impl WalletPlatform for Platform {
    fn version(&self) -> String {
        let mut buffer = [0; 32];
        unsafe { app_version_get(buffer.as_mut_ptr(), buffer.len()) };
        CStr::from_bytes_until_nul(&buffer)
            .ok()
            .and_then(|value| value.to_str().ok())
            .unwrap_or("unknown")
            .into()
    }

    fn serial_number(&self) -> String {
        let mut chip = [0; 32];
        let mut eui64 = [0; 8];
        let mut device_id = [0; 16];

        unsafe {
            app_get_chip_model(chip.as_mut_ptr() as *mut c_char, chip.len());
            app_get_eui64(eui64.as_mut_ptr(), eui64.len());
        }
        let device_id_len = unsafe { app_get_device_id(device_id.as_mut_ptr(), device_id.len()) }
            .clamp(0, device_id.len() as i32) as usize;
        let chip = CStr::from_bytes_until_nul(&chip)
            .ok()
            .and_then(|value| value.to_str().ok())
            .unwrap_or("UNKNOWN");

        let mut serial = format!("{chip}-");
        for byte in eui64 {
            let _ = write!(serial, "{byte:02x}");
        }
        serial.push('-');
        for byte in &device_id[..device_id_len] {
            let _ = write!(serial, "{byte:02x}");
        }
        serial
    }

    fn support_mask(&self) -> Vec<u8> {
        let mut buffer = [0; 16];
        if unsafe { app_check_feature(buffer.as_mut_ptr(), buffer.len()) } {
            buffer.to_vec()
        } else {
            Vec::new()
        }
    }

    fn local_ui_enabled(&self) -> bool {
        unsafe { app_display_ready() }
    }

    fn storage_ready(&self) -> bool {
        unsafe { app_check_storage() }
    }

    fn seed_exists(&self) -> Result<bool> {
        match unsafe { storage_general_check(storage_ids.seed) } {
            0 => Ok(false),
            1 => Ok(true),
            result => Err(anyhow!("Failed to check seed: {result}")),
        }
    }

    fn random(&self, len: usize) -> Vec<u8> {
        let mut data = alloc::vec![0; len];
        if !unsafe { app_csrand_get(data.as_mut_ptr(), data.len()) } {
            data.clear();
        }
        data
    }

    fn read_seed(&self, data: &mut [u8]) -> Result<usize> {
        let result =
            unsafe { storage_general_read(data.as_mut_ptr(), data.len(), storage_ids.seed) };
        if result < 0 {
            Err(anyhow!("Failed to read seed: {result}"))
        } else {
            Ok(result as usize)
        }
    }

    fn write_seed(&self, data: &[u8]) -> Result<()> {
        if unsafe { storage_general_write(data.as_ptr(), data.len(), storage_ids.seed) } {
            Ok(())
        } else {
            Err(anyhow!("Failed to store seed"))
        }
    }

    fn unlock_failures(&self) -> Result<u8> {
        match unsafe { storage_general_check(storage_ids.unlock_failures) } {
            0 => return Ok(0),
            1 => {}
            result => return Err(anyhow!("Failed to check unlock counter: {result}")),
        }

        let mut failures = 0;
        let read = unsafe {
            storage_general_read(
                &mut failures,
                size_of_val(&failures),
                storage_ids.unlock_failures,
            )
        };
        if read == size_of_val(&failures) as i32 {
            Ok(failures)
        } else {
            Err(anyhow!("Failed to read unlock counter: {read}"))
        }
    }

    fn write_unlock_failures(&self, failures: u8) -> bool {
        unsafe {
            storage_general_write(
                &failures,
                size_of_val(&failures),
                storage_ids.unlock_failures,
            )
        }
    }

    fn reset_storage(&self) -> bool {
        unsafe { app_storage_reset() }
    }

    fn restart(&self) {
        unsafe { app_restart() };
    }
}
