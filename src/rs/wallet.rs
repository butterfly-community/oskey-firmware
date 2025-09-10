use alloc::string::ToString;
use anyhow::{anyhow, Result};
use core::ffi::c_char;
use oskey_bus::proto;
use oskey_bus::proto::{req_data::Payload, res_data, ReqData, ResData};
use oskey_bus::Message;
extern crate alloc;
extern crate zephyr;

static mut GLOBAL_SIGN: Option<proto::SignRequest> = None;

#[allow(unused_doc_comments)]
/// cbindgen:ignore
extern "C" {
    pub(crate) fn app_cs_random(dst: *mut u8, len: usize) -> bool;
    pub(crate) fn app_uart_tx_push_array(data: *const u8, len: usize);
    pub(crate) fn app_version_get(data: *mut u8, len: usize) -> bool;
    pub(crate) fn app_check_support(number: u32) -> bool;
    pub(crate) fn app_check_lock() -> bool;
    pub(crate) fn app_display_sign(text: *const c_char);
    pub(crate) fn storage_general_check(id: u16) -> bool;
    pub(crate) fn storage_seed_write(data: *const u8, len: usize, phrase_len: usize) -> bool;
    pub(crate) fn storage_seed_read(data: *mut u8, len: usize) -> bool;
}

#[no_mangle]
pub static CHECK_INPUT_DISPLAY: u32 = 0;

#[no_mangle]
extern "C" fn event_bytes_handle(bytes: *mut u8, len: usize) {
    let bytes = unsafe { core::slice::from_raw_parts(bytes, len) };
    let _event = event_parser(bytes);
    return;
}

#[no_mangle]
extern "C" fn storage_seed_check() -> bool {
    let check = unsafe { storage_general_check(2) };
    return check;
}

pub fn event_parser(bytes: &[u8]) -> Result<()> {
    let parser = oskey_bus::FrameParser::unpack(bytes)?;
    let payload_bytes = parser.ok_or(anyhow!("Waiting"))?;
    let req_data = ReqData::decode(payload_bytes.as_slice()).map_err(|e| anyhow!(e))?;
    let _res = event_hub(req_data)?;
    Ok(())
}

pub fn event_hub(req: ReqData) -> Result<()> {
    if unsafe { app_check_lock() } {
        return Err(anyhow!("Device Locked"));
    }

    let res_payload = match req.payload {
        Some(payload) => payload,
        None => return Err(anyhow!("ERROR")),
    };

    let payload = match res_payload {
        Payload::Unknown(_unknown) => Some(oskey_action::wallet_unknown_req()),
        Payload::VersionRequest(_) => Some(oskey_action::wallet_version_req(
            app_version_get,
            storage_seed_check,
        )),
        Payload::InitRequest(data) => Some(oskey_action::wallet_init_default(
            data,
            app_cs_random,
            true,
            storage_seed_write,
        )?),
        Payload::InitCustomRequest(data) => {
            Some(oskey_action::wallet_init_custom(data, storage_seed_write)?)
        }
        Payload::DerivePublicKeyRequest(data) => Some(oskey_action::wallet_drive_public_key(
            data,
            storage_seed_read,
        )?),
        Payload::SignRequest(data) => {
            let check = unsafe { app_check_support(CHECK_INPUT_DISPLAY) };
            if !check {
                Some(oskey_action::wallet_sign_msg(data, storage_seed_read)?)
            } else {
                unsafe {
                    #[allow(static_mut_refs)]
                    drop(GLOBAL_SIGN.take());
                    GLOBAL_SIGN = Some(data.clone());
                    let mut c_string = data.debug_text.unwrap_or_default();
                    c_string.push('\0');
                    app_display_sign(c_string.as_ptr() as *const c_char);
                }
                None
            }
        }
    };

    event_sent_res(payload)?;

    Ok(())
}

pub fn event_sent_res(payload: Option<res_data::Payload>) -> Result<()> {
    if payload.is_none() {
        return Err(anyhow!("ERROR"));
    }

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

    let exec =
        match oskey_action::wallet_init_default(res, app_cs_random, false, storage_seed_write) {
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

#[no_mangle]
#[allow(static_mut_refs)]
extern "C" fn wallet_sign_display() -> bool {
    let payload = unsafe {
        match GLOBAL_SIGN.take() {
            Some(payload) => payload,
            None => return false,
        }
    };
    let res = oskey_action::wallet_sign_msg(payload, storage_seed_read);

    event_sent_res(res.ok()).is_ok()
}
