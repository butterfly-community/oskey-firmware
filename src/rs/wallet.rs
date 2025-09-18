extern crate alloc;
extern crate zephyr;

use alloc::format;
use alloc::string::String;
use alloc::string::ToString;
use anyhow::{anyhow, Result};
use core::ffi::c_char;
use oskey_bus::proto;
use oskey_bus::proto::{req_data::Payload, res_data, ReqData, ResData};
use oskey_bus::Message;
use oskey_chain::eth::OSKeyTxEip2930;
use zephyr::sync::SpinMutex;

static ETH_SIGN_CACHE: EthSignRequestCache = EthSignRequestCache::new();

pub struct EthSignRequestCache {
    req: SpinMutex<Option<proto::SignEthRequest>>,
}

#[allow(unused)]
impl EthSignRequestCache {
    pub const fn new() -> Self {
        Self {
            req: SpinMutex::new(None),
        }
    }

    pub fn store(&self, request: proto::SignEthRequest) -> Result<()> {
        *self.req.lock()? = Some(request);
        Ok(())
    }

    pub fn has(&self) -> Result<bool> {
        Ok(self.req.lock()?.is_some())
    }

    pub fn clear(&self) -> Result<()> {
        *self.req.lock()? = None;
        Ok(())
    }

    pub fn peek(&self) -> Result<Option<proto::SignEthRequest>> {
        Ok(self.req.lock()?.clone())
    }

    pub fn display(&self) -> Result<String> {
        let guard = self.peek()?.ok_or_else(|| anyhow!("Not found"))?;
        let tx = guard.tx.ok_or_else(|| anyhow!("Not found"))?;
        let mut text = match tx {
            proto::sign_eth_request::Tx::Eip2930(app_eth_tx_eip2930) => {
                OSKeyTxEip2930::from_proto(app_eth_tx_eip2930)?.to_string()
            }
            proto::sign_eth_request::Tx::Eip191(app_eth_msg_sign) => {
                if app_eth_msg_sign.message.len() > 1024 {
                    app_eth_msg_sign.message[..256].to_string() + "..."
                } else {
                    app_eth_msg_sign.message
                }
            }
        };
        text.push('\0');
        Ok(text)
    }
}
#[allow(unused_doc_comments)]
/// cbindgen:ignore
extern "C" {
    pub(crate) fn app_csrand_get(dst: *mut u8, len: usize) -> bool;
    pub(crate) fn app_uart_tx_push_array(data: *const u8, len: usize);
    pub(crate) fn app_version_get(data: *mut u8, len: usize) -> bool;
    pub(crate) fn app_check_feature(data: *mut u8, len: usize) -> bool;
    pub(crate) fn app_check_lock() -> bool;
    pub(crate) fn app_display_sign(text: *const c_char);
    pub(crate) fn storage_general_check(id: u16) -> bool;
    pub(crate) fn storage_general_read(data: *mut u8, len: usize, id: u16) -> bool;
    pub(crate) fn storage_general_write(data: *const u8, len: usize, id: u16) -> bool;
}

#[no_mangle]
pub static STORAGE_ID_SEED: u16 = 2;
#[no_mangle]
pub static STORAGE_ID_PIN: u16 = 10;

#[no_mangle]
extern "C" fn app_version_get_rs(data: *mut u8, len: usize) -> bool {
    return unsafe { app_version_get(data, len) };
}

#[no_mangle]
extern "C" fn app_csrand_get_rs(bytes: *mut u8, len: usize) -> bool {
    return unsafe { app_csrand_get(bytes, len) };
}

#[no_mangle]
extern "C" fn event_bytes_handle(bytes: *mut u8, len: usize) {
    let bytes = unsafe { core::slice::from_raw_parts(bytes, len) };
    let _event = event_parser(bytes);
    return;
}

#[no_mangle]
extern "C" fn storage_seed_check() -> bool {
    let check = unsafe { storage_general_check(STORAGE_ID_SEED) };
    return check;
}

#[no_mangle]
extern "C" fn storage_seed_read(data: *mut u8, len: usize) -> bool {
    let check = unsafe { storage_general_read(data, len, STORAGE_ID_SEED) };
    return check;
}

#[no_mangle]
extern "C" fn storage_seed_write(data: *const u8, len: usize, _phrase_len: usize) -> bool {
    let check = unsafe { storage_general_write(data, len, STORAGE_ID_SEED) };
    return check;
}

#[allow(dead_code)]
enum Feature {
    SecureBoot = 0,
    FlashEncryption = 1,
    Bootloader = 2,
    StorageInit = 3,
    HardwareRng = 4,
    DisplayInput = 5,
    UserKey = 6,
}

fn app_check_feature_rs(mask: Feature) -> bool {
    let mut buffer = [0u8; 16];

    if !unsafe { app_check_feature(buffer.as_mut_ptr(), buffer.len()) } {
        return false;
    }

    let index = mask as usize;
    index < buffer.len() && buffer[index] != 0
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
        Payload::Unknown(_unknown) => Ok(oskey_action::wallet_unknown_req()),
        Payload::VersionRequest(_) => app_wallet_version_req(),
        Payload::InitRequest(data) => {
            oskey_action::wallet_init_default(data, app_csrand_get_rs, true, storage_seed_write)
        }
        Payload::InitCustomRequest(data) => {
            oskey_action::wallet_init_custom(data, storage_seed_write)
        }
        Payload::DerivePublicKeyRequest(data) => {
            oskey_action::wallet_drive_public_key(data, storage_seed_read)
        }
        Payload::SignEthRequest(data) => app_wallet_sign_eth_req(data),
    };

    app_event_sent_res(payload);

    Ok(())
}

pub fn app_event_sent_res(payload: Result<res_data::Payload>) {
    let data = payload.unwrap_or_else(|e| {
        #[cfg(debug_assertions)]
        let error_msg = format!("{:?}", e);
        #[cfg(not(debug_assertions))]
        let error_msg = format!("{:#}", e);
        res_data::Payload::ErrorResponse(proto::ErrorResponse {
            code: 0,
            message: error_msg,
        })
    });

    let response = ResData {
        payload: data.into(),
    };

    let bytes = response.encode_to_vec();

    let pack = oskey_bus::FrameParser::pack(&bytes);

    unsafe {
        app_uart_tx_push_array(pack.as_ptr(), pack.len());
    }
}

fn app_wallet_sign_eth_req(data: proto::SignEthRequest) -> Result<res_data::Payload> {
    ETH_SIGN_CACHE.store(data)?;

    let check_display = app_check_feature_rs(Feature::DisplayInput);
    if !check_display {
        return app_wallet_sign_eth();
    }
    unsafe {
        app_display_sign(ETH_SIGN_CACHE.display()?.as_ptr() as *const c_char);
    }
    return Ok(res_data::Payload::WaitForUserActionResponse(
        proto::WaitForUserActionResponse {},
    ));
}

fn app_wallet_sign_eth() -> Result<res_data::Payload> {
    let data = ETH_SIGN_CACHE.peek()?.ok_or(anyhow!("Cache Not found"))?;
    let tx = data.tx.ok_or(anyhow!("Tx Not found"))?;

    let hash = match tx {
        proto::sign_eth_request::Tx::Eip2930(app_eth_tx_eip2930) => {
            let tx = OSKeyTxEip2930::from_proto(app_eth_tx_eip2930)?;
            tx.hash()
        }
        proto::sign_eth_request::Tx::Eip191(app_eth_msg_sign) => {
            let message = app_eth_msg_sign.message;
            let hash = oskey_chain::eth::OSKeyTxEip191::hash_message(message.as_bytes());
            hash
        }
    };

    oskey_action::wallet_sign_keccak256(data.id, data.path, hash, storage_seed_read)
}

fn app_wallet_version_req() -> Result<res_data::Payload> {
    let mut buffer = [0u8; 16];

    if !unsafe { app_check_feature(buffer.as_mut_ptr(), buffer.len()) } {
        return Err(anyhow!("ERROR"));
    }

    let res =
        oskey_action::wallet_version_req(buffer.to_vec(), app_version_get_rs, storage_seed_check);

    return Ok(res);
}

#[no_mangle]
extern "C" fn wallet_init_default_from_display(
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

    let exec = match oskey_action::wallet_init_default(
        res,
        app_csrand_get_rs,
        false,
        storage_seed_write,
    ) {
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
extern "C" fn wallet_init_custom_from_display(
    mnemonic: *const c_char,
    password: *const c_char,
) -> bool {
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
extern "C" fn wallet_sign_eth_from_display() -> bool {
    let res = app_wallet_sign_eth();
    let check = res.is_ok();
    app_event_sent_res(res);
    check
}
