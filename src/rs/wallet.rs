use alloc::string::ToString;
use anyhow::{anyhow, Result};
use core::ffi::c_char;
use oskey_bus::proto;
use oskey_bus::proto::{req_data::Payload, res_data, ReqData, ResData};
use oskey_bus::Message;

pub extern crate alloc;
pub extern crate zephyr;

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

pub fn event_parser(bytes: &[u8]) -> Result<()> {
    let parser = oskey_bus::FrameParser::unpack(bytes)?;
    let payload_bytes = parser.ok_or(anyhow!("Waiting"))?;
    let req_data = ReqData::decode(payload_bytes.as_slice()).map_err(|e| anyhow!(e))?;
    let _res = event_hub(req_data).unwrap();
    Ok(())
}

pub fn event_hub(req: ReqData) -> Result<()> {
    let payload = match req.payload.ok_or(anyhow!("Fail"))? {
        Payload::Unknown(_unknown) => oskey_action::wallet_unknown_req(),
        Payload::VersionRequest(_) => {
            oskey_action::wallet_version_req(app_version_get, storage_seed_check)
        }
        Payload::InitRequest(data) => {
            oskey_action::wallet_init_default(data, app_cs_random, true, storage_seed_write)?
        }
        Payload::InitCustomRequest(data) => {
            oskey_action::wallet_init_custom(data, storage_seed_write)?
        }
        Payload::DerivePublicKeyRequest(data) => {
            oskey_action::wallet_drive_public_key(data, storage_seed_read)?
        }
        Payload::SignRequest(data) => oskey_action::wallet_sign_msg(data, storage_seed_read)?,
    };

    let response = ResData {
        payload: payload.into(),
    };

    let bytes = response.encode_to_vec();

    let pack = oskey_bus::FrameParser::pack(&bytes);

    unsafe {
        app_uart_tx_push_array(pack.as_ptr(), pack.len());
    }

    Ok(())
}

#[no_mangle]
extern "C" fn wallet_init_default_display(
    mnemonic_length: usize,
    password: *const c_char,
    buffer: *mut c_char,
    len: usize,
) -> bool {
    let res = proto::InitWalletRequest {
        length: mnemonic_length as u32,
        password: unsafe {
            let c_str = core::ffi::CStr::from_ptr(password);
            c_str.to_str().unwrap_or("").to_string()
        },
        seed: None,
    };

    let exec = match oskey_action::wallet_init_default(res, app_cs_random, false, storage_seed_write) {
        Ok(v) => v,
        Err(_) => return false,
    };

    match exec {
        res_data::Payload::InitWalletResponse(r) => {
            let s = r.mnemonic.unwrap_or_default();
            let bytes = s.as_bytes();
            if bytes.len() > len {
                return false;
            }
            unsafe {
                core::ptr::copy_nonoverlapping(bytes.as_ptr(), buffer as *mut u8, bytes.len());
                *(buffer.add(bytes.len())) = 0;
            }
        }
        _ => return false,
    }
    return true;
}

#[no_mangle]
extern "C" fn wallet_init_custom_display(mnemonic: *const c_char, password: *const c_char) -> bool {
    let res = proto::InitWalletCustomRequest {
        words: unsafe {
            let c_str = core::ffi::CStr::from_ptr(mnemonic);
            c_str.to_str().unwrap_or("").to_string()
        },
        password: unsafe {
            let c_str = core::ffi::CStr::from_ptr(password);
            c_str.to_str().unwrap_or("").to_string()
        },
    };

    match oskey_action::wallet_init_custom(res, storage_seed_write) {
        Ok(v) => v,
        Err(_) => return false,
    };
    return true;
}
