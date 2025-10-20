use alloc::string::{String, ToString};
use alloc::{vec, vec::Vec};
use anyhow::{anyhow, Result};
use core::ffi::c_char;
use oskey_action::WalletCallbacks;
use oskey_bus::proto::{
    self, req_data::Payload, res_data, InitWalletCustomRequest, InitWalletRequest, ReqData,
};

use crate::rs::bus::app_event_sent_res;
use crate::rs::chain::eth::ETH_SIGN_CACHE;
use crate::rs::storage::{STORAGE_ID_SEED, WALLET_STORAGE};
use crate::rs::warpper::{
    app_check_feature_rs, app_check_feature_vec, app_check_status_rs, app_check_status_vec,
    app_csrand_get_rs, app_display_sign, app_get_device_info_rs, storage_general_check, Feature,
    Status,
};

pub struct OSKeyHWCallbacks;

impl WalletCallbacks for OSKeyHWCallbacks {
    fn version(&self) -> String {
        crate::rs::warpper::app_version_get_rs()
    }

    fn sn(&self) -> String {
        app_get_device_info_rs()
    }

    fn initialized(&self) -> bool {
        unsafe { storage_general_check(STORAGE_ID_SEED) }
    }

    fn support_mask(&self) -> Vec<u8> {
        app_check_feature_vec()
    }

    fn status_mask(&self) -> Vec<u8> {
        app_check_status_vec()
    }

    fn random(&self, len: usize) -> Vec<u8> {
        let mut buffer = vec![0u8; len];
        app_csrand_get_rs(&mut buffer);
        buffer
    }

    fn save_seed(&self, seed: &[u8], _phrase_len: usize) -> Result<()> {
        let result = WALLET_STORAGE.write_seed(seed, seed.len());
        if result < 0 {
            return Err(anyhow!("Failed to save seed: {}", result));
        }
        Ok(())
    }

    fn load_seed(&self) -> Vec<u8> {
        let mut buffer = vec![0u8; 64];
        let len = WALLET_STORAGE.read_seed(buffer.as_mut_ptr(), buffer.len());
        if len > 0 {
            buffer.truncate(len as usize);
        }
        buffer
    }
}

pub fn wallet_handle_hub(req: ReqData) -> Result<res_data::Payload> {
    let callbacks = OSKeyHWCallbacks;

    let req_payload = match req.payload {
        Some(payload) => payload,
        None => return Ok(oskey_action::handle_unknown()),
    };

    if let Payload::VersionRequest(_) = req_payload {
        return Ok(oskey_action::handle_version(&callbacks));
    }

    if let Payload::StatusRequest(_) = req_payload {
        return Ok(oskey_action::handle_status(&callbacks));
    }

    if app_check_feature_rs(Feature::DisplayInput) {
        if matches!(
            &req_payload,
            Payload::UnlockRequest(_) | Payload::InitCustomRequest(_) | Payload::InitRequest(_)
        ) {
            return Err(anyhow!(
                "Feature DisplayInput enabled, Please use display to operate"
            ));
        }
    }

    if let Payload::UnlockRequest(data) = req_payload {
        WALLET_STORAGE.unlock_from_hash(data.hash);
        return Ok(oskey_action::handle_status(&callbacks));
    }

    if app_check_status_rs(Status::Locked) {
        return Err(anyhow!("Wallet is locked"));
    }

    let payload = match req_payload {
        Payload::Unknown(_unknown) => Ok(oskey_action::handle_unknown()),
        Payload::DerivePublicKeyRequest(data) => {
            oskey_action::handle_derive_public_key(data, &callbacks)
        }
        Payload::InitRequest(data) => {
            WALLET_STORAGE.set_pin_from_hash(data.pin.clone());
            oskey_action::handle_init_wallet(data, &callbacks, true)
        }
        Payload::InitCustomRequest(data) => {
            WALLET_STORAGE.set_pin_from_hash(data.pin.clone());
            oskey_action::handle_init_wallet_custom(data, &callbacks)
        }
        Payload::SignEthRequest(data) => {
            ETH_SIGN_CACHE.store(data)?;
            unsafe {
                app_display_sign(ETH_SIGN_CACHE.display()?.as_ptr() as *const c_char);
            }
            Ok(res_data::Payload::WaitForUserActionResponse(
                proto::WaitForUserActionResponse {},
            ))
        }
        _ => Ok(oskey_action::handle_unknown()),
    };

    payload
}

#[no_mangle]
extern "C" fn wallet_sign_eth_from_trigger() -> bool {
    let res = ETH_SIGN_CACHE.sign();
    app_event_sent_res(res);
    return true;
}

#[no_mangle]
extern "C" fn wallet_mnemonic_generate_from_display(
    mnemonic_length: usize,
    buffer: *mut c_char,
    len: usize,
    entry: *const u8,
    custom_mode: bool,
) -> bool {
    let mnemonic = if !custom_mode {
        let callbacks = OSKeyHWCallbacks;

        oskey_action::handle_init_wallet(
            InitWalletRequest {
                length: mnemonic_length as u32,
                pin: vec![],
                password: "".to_string(),
                seed: None,
            },
            &callbacks,
            false,
        )
    } else {
        oskey_action::handle_init_wallet_custom_entropy(InitWalletRequest {
            length: mnemonic_length as u32,
            pin: vec![],
            password: "".to_string(),
            seed: Some(unsafe {
                let entropy_len = match mnemonic_length {
                    12 => 16,
                    15 => 20,
                    18 => 24,
                    21 => 28,
                    24 => 32,
                    _ => 16,
                };
                let slice = core::slice::from_raw_parts(entry, entropy_len);
                slice.to_vec()
            }),
        })
    };

    let mnemonic = match mnemonic {
        Ok(res_data::Payload::InitWalletResponse(resp)) => match resp.mnemonic {
            Some(mnemonic) => mnemonic,
            None => return false,
        },
        _ => return false,
    };

    unsafe {
        let bytes = mnemonic.as_bytes();
        if bytes.len() > len {
            return false;
        }
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), buffer as *mut u8, bytes.len());
        *(buffer.add(bytes.len())) = 0;
    };

    return true;
}

#[no_mangle]
extern "C" fn wallet_init_custom_from_display(mnemonic: *const c_char) -> bool {
    let callbacks = OSKeyHWCallbacks;
    let words = unsafe {
        let c_str = core::ffi::CStr::from_ptr(mnemonic);
        c_str.to_str().unwrap_or("").to_string()
    };
    let exec = oskey_action::handle_init_wallet_custom(
        InitWalletCustomRequest {
            words,
            password: "".to_string(),
            pin: vec![],
        },
        &callbacks,
    );

    let Ok(payload) = exec else {
        return false;
    };

    let res_data::Payload::InitWalletResponse(resp) = payload else {
        return false;
    };

    return resp.mnemonic.is_some();
}
