extern crate alloc;
extern crate zephyr;

use alloc::format;
use alloc::string::String;
use alloc::string::ToString;
use alloc::vec;
use alloc::vec::Vec;
use anyhow::{anyhow, Result};
use core::ffi::c_char;
use core::ffi::c_int;
use core::ffi::CStr;
use oskey_bus::proto;
use oskey_bus::proto::{req_data::Payload, res_data, ReqData, ResData};
use oskey_bus::FrameParser;
use oskey_bus::Message;
use oskey_chain::eth::OSKeyTxEip2930;
use oskey_wallet::mnemonic;
use oskey_wallet::wallets;
use zephyr::sync::SpinMutex;

static ETH_SIGN_CACHE: EthSignRequestCache = EthSignRequestCache::new();

static APP_UART_REQ_PARSER: AppDataParser = AppDataParser::new();

static PIN_CACHE: SpinMutex<[u8; 32]> = SpinMutex::new([0u8; 32]);

static LOCK_MARK: SpinMutex<bool> = SpinMutex::new(false);

#[no_mangle]
extern "C" fn lock_mark_get() -> bool {
    match LOCK_MARK.lock() {
        Ok(guard) => *guard,
        Err(_) => false,
    }
}

#[no_mangle]
extern "C" fn lock_mark_set(value: bool) {
    match LOCK_MARK.lock() {
        Ok(mut guard) => *guard = value,
        Err(_) => {}
    }
}

#[no_mangle]
extern "C" fn lock_mark_lock() {
    lock_mark_set(true);
}

#[no_mangle]
extern "C" fn lock_mark_unlock() {
    lock_mark_set(false);
}

pub struct EthSignRequestCache {
    req: SpinMutex<Option<proto::SignEthRequest>>,
}

pub struct AppDataParser {
    parser: SpinMutex<FrameParser>,
}

#[allow(unused)]
impl AppDataParser {
    pub const fn new() -> Self {
        Self {
            parser: SpinMutex::new(FrameParser::new()),
        }
    }

    pub fn store(&self, parser: FrameParser) -> Result<()> {
        *self.parser.lock()? = parser;
        Ok(())
    }

    pub fn push(&self, data: &[u8]) -> Option<Result<ReqData>> {
        match self.parser.lock() {
            Ok(mut guard) => guard.push(data),
            Err(e) => Some(Err(anyhow!(e))),
        }
    }

    pub fn unpack(&self) -> Option<Result<ReqData>> {
        match self.parser.lock() {
            Ok(mut guard) => guard.unpack(),
            Err(e) => Some(Err(anyhow!(e))),
        }
    }

    pub fn push_check(&self, data: &[u8]) -> bool {
        match self.parser.lock() {
            Ok(mut guard) => guard.push_check(data),
            Err(_) => false,
        }
    }

    pub fn len(&self) -> Result<usize> {
        Ok(self.parser.lock()?.buffer.len())
    }
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
                    app_eth_msg_sign.message[..512].to_string() + "..."
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
    pub(crate) fn app_check_status(data: *mut u8, len: usize) -> bool;
    pub(crate) fn app_display_sign(text: *const c_char);
    pub(crate) fn storage_general_check(id: u16) -> bool;
    pub(crate) fn storage_general_read(data: *mut u8, len: usize, id: u16) -> c_int;
    pub(crate) fn storage_general_write(data: *const u8, len: usize, id: u16) -> bool;
}

#[no_mangle]
pub static STORAGE_ID_SEED: u16 = 2;
#[no_mangle]
pub static STORAGE_ID_PIN: u16 = 10;
#[no_mangle]
pub static PASSWORD_SALT_FIRST: &[u8] = b"&%OSKey1$!@";

fn app_pin_gen(hash: [u8; 32]) -> Result<[u8; 64]> {
    let seed = oskey_wallet::mnemonic::Mnemonic::from_entropy(&hash)?;
    return seed.to_seed("OSKey");
}

#[no_mangle]
fn app_pin_text_to_key(password: *const c_char) -> Result<[u8; 64]> {
    let password = unsafe {
        let c_str = core::ffi::CStr::from_ptr(password);
        c_str.to_str().unwrap_or("").to_string()
    };
    let salt = PASSWORD_SALT_FIRST;
    let hash = oskey_wallet::alg::crypto::Hash::sha256(&[password.as_bytes(), salt].concat())?;
    return app_pin_gen(hash);
}

#[no_mangle]
fn app_pin_hash_to_key(hash: [u8; 32]) -> Result<[u8; 64]> {
    return app_pin_gen(hash);
}

#[no_mangle]
extern "C" fn app_version_get_rs(data: *mut u8, len: usize) -> bool {
    return unsafe { app_version_get(data, len) };
}

#[no_mangle]
extern "C" fn app_csrand_get_rs(bytes: *mut u8, len: usize) -> bool {
    return unsafe { app_csrand_get(bytes, len) };
}

#[no_mangle]
extern "C" fn storage_seed_check_rs() -> bool {
    let check = unsafe { storage_general_check(STORAGE_ID_SEED) };
    return check;
}

#[no_mangle]
extern "C" fn storage_seed_read_rs(data: *mut u8, len: usize) -> c_int {
    let mut buffer = vec![0u8; 128];

    let check = unsafe { storage_general_read(buffer.as_mut_ptr(), buffer.len(), STORAGE_ID_SEED) };
    if check < 0 {
        return check;
    }

    let pin = match PIN_CACHE.lock() {
        Ok(guard) => *guard,
        Err(_) => return -2000,
    };

    let mut nonce = [0u8; 12];
    nonce.copy_from_slice(&buffer[0..12]);

    let secret = &buffer[12..check as usize];

    let real =
        match oskey_wallet::alg::crypto::ChaCha20Poly1305Cipher::decrypt(&pin, &nonce, secret) {
            Ok(v) => v,
            Err(_) => return -2001,
        };
    if real.len() > len {
        return -2002;
    }
    unsafe {
        core::ptr::copy_nonoverlapping(real.as_ptr(), data, real.len());
    }
    return real.len() as c_int;
}

fn storage_seed_write_rs(data: &[u8], len: usize) -> c_int {
    let pin = *PIN_CACHE.lock().unwrap();
    let mut nonce = [0u8; 12];
    app_csrand_get_rs(nonce.as_mut_ptr(), nonce.len());

    let encrypt = match oskey_wallet::alg::crypto::ChaCha20Poly1305Cipher::encrypt(
        &pin,
        &nonce,
        &data[..len],
    ) {
        Ok(v) => v,
        Err(_) => return -2000,
    };

    let mut secret = Vec::with_capacity(nonce.len() + encrypt.len());
    secret.extend_from_slice(&nonce);
    secret.extend_from_slice(&encrypt);

    let check = unsafe { storage_general_write(secret.as_ptr(), secret.len(), STORAGE_ID_SEED) };
    if !check {
        return -2001;
    }
    return 0;
}

#[no_mangle]
extern "C" fn app_uart_event_rs(data: *mut u8, len: usize) -> bool {
    return APP_UART_REQ_PARSER.push_check(unsafe { core::slice::from_raw_parts(data, len) });
}

#[no_mangle]
extern "C" fn app_event_bytes_handle() {
    let _event = app_event_trans();
    return;
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

#[allow(dead_code)]
enum Status {
    StorageInit = 0,
    Locked = 1,
}

#[allow(dead_code)]
fn app_check_status_rs(mask: Status) -> bool {
    let mut buffer = [0u8; 16];

    if !unsafe { app_check_status(buffer.as_mut_ptr(), buffer.len()) } {
        return false;
    }

    let index = mask as usize;
    index < buffer.len() && buffer[index] != 0
}

// Not irq, should be called in main loop or work thread
pub fn app_event_trans() -> Result<()> {
    let test = APP_UART_REQ_PARSER.unpack();
    let req_data = test.ok_or(anyhow!("Waiting"))??;
    let payload = event_hub(req_data);
    app_event_sent_res(payload);
    Ok(())
}

#[allow(dead_code)]
pub fn event_hub(req: ReqData) -> Result<res_data::Payload> {
    let req_payload = match req.payload {
        Some(payload) => payload,
        None => return Ok(wallet_unknown_req()),
    };

    if let Payload::VersionRequest(_) = req_payload {
        return app_wallet_version_req();
    }

    if let Payload::StatusRequest(_) = req_payload {
        return app_wallet_status_req();
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
        wallet_unlock_from_proto(data.hash);
        return app_wallet_status_req();
    }

    if app_check_status_rs(Status::Locked) {
        return Err(anyhow!("Wallet is locked"));
    }

    let payload = match req_payload {
        Payload::Unknown(_unknown) => Ok(wallet_unknown_req()),
        Payload::DerivePublicKeyRequest(data) => {
            wallet_drive_public_key(data, storage_seed_read_rs)
        }
        Payload::InitRequest(data) => wallet_init_default_from_proto(data.length, data.pin),
        Payload::InitCustomRequest(data) => wallet_init_custom_from_proto(data.words, data.pin),
        Payload::SignEthRequest(data) => app_wallet_sign_eth_req(data),
        _ => Ok(wallet_unknown_req()),
    };

    payload
}

pub type VersionCallback = extern "C" fn(data: *mut u8, len: usize) -> bool;
pub type CheckInitCallback = extern "C" fn() -> bool;
pub type GetSeedStorageCallback = extern "C" fn(data: *mut u8, len: usize) -> c_int;

pub fn wallet_version_req(
    support: Vec<u8>,
    version_cb: VersionCallback,
    check_init_cb: CheckInitCallback,
) -> res_data::Payload {
    let mut buffer = vec![0u8; 10];

    version_cb(buffer.as_mut_ptr(), buffer.len());

    let init_check = check_init_cb();

    let features = oskey_bus::proto::Features {
        initialized: init_check,
        support_mask: support,
    };

    let version = oskey_bus::proto::VersionResponse {
        version: String::from(
            CStr::from_bytes_until_nul(&buffer)
                .unwrap_or(CStr::from_bytes_with_nul(b"unknown\0").unwrap())
                .to_str()
                .unwrap_or("unknown"),
        ),
        features: features.into(),
    };

    let payload = res_data::Payload::VersionResponse(version);
    return payload;
}

pub fn app_mnemonic_generate_rs(length: u32) -> Result<mnemonic::Mnemonic> {
    let need_len = length as usize * 4 / 3;

    let mut buffer = vec![0u8; need_len];

    app_csrand_get_rs(buffer.as_mut_ptr(), need_len);

    let mnemonic = mnemonic::Mnemonic::from_entropy(&buffer)?;

    Ok(mnemonic)
}

pub fn wallet_drive_public_key(
    data: proto::DerivePublicKeyRequest,
    seed_storage_cb: GetSeedStorageCallback,
) -> Result<res_data::Payload> {
    let mut buffer = vec![0u8; 64];

    seed_storage_cb(buffer.as_mut_ptr(), buffer.len());

    let ex_priv_key = wallets::ExtendedPrivKey::derive(
        &buffer,
        data.path.parse()?,
        oskey_wallet::wallets::Curve::K256,
    )?;

    let pk = ex_priv_key.export_pk()?;

    let data = proto::DerivePublicKeyResponse {
        path: data.path,
        public_key: pk.to_vec(),
    };

    let payload = res_data::Payload::DerivePublicKeyResponse(data);

    return Ok(payload);
}

pub fn wallet_unknown_req() -> res_data::Payload {
    return res_data::Payload::Unknown(proto::Unknown {});
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

#[allow(dead_code)]
fn app_wallet_sign_eth_req(data: proto::SignEthRequest) -> Result<res_data::Payload> {
    ETH_SIGN_CACHE.store(data)?;

    let check_display = app_check_feature_rs(Feature::DisplayInput);
    if !check_display {
        let sign = app_wallet_sign_eth();
        ETH_SIGN_CACHE.clear()?;
        return sign;
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

    wallet_sign_keccak256(data.id, data.path, hash, storage_seed_read_rs)
}

fn app_wallet_version_req() -> Result<res_data::Payload> {
    let mut buffer = [0u8; 16];

    if !unsafe { app_check_feature(buffer.as_mut_ptr(), buffer.len()) } {
        return Err(anyhow!("ERROR"));
    }

    let res = wallet_version_req(buffer.to_vec(), app_version_get_rs, storage_seed_check_rs);

    return Ok(res);
}

fn app_wallet_status_req() -> Result<res_data::Payload> {
    let mut buffer = [0u8; 4];
    if !unsafe { app_check_status(buffer.as_mut_ptr(), buffer.len()) } {
        return Err(anyhow!("ERROR"));
    }
    return Ok(res_data::Payload::StatusResponse(proto::StatusResponse {
        status_mask: buffer.to_vec(),
    }));
}

pub fn wallet_sign_keccak256(
    id: i32,
    path: String,
    hash: [u8; 32],
    seed_storage_cb: GetSeedStorageCallback,
) -> Result<res_data::Payload> {
    let mut buffer = vec![0u8; 64];

    seed_storage_cb(buffer.as_mut_ptr(), buffer.len());

    let ex_priv_key = wallets::ExtendedPrivKey::derive(
        &buffer,
        path.parse()?,
        oskey_wallet::wallets::Curve::K256,
    )?;

    let sign = ex_priv_key.sign(&hash)?;

    let data = proto::SignResponse {
        id: id,
        message: "".into(),
        public_key: ex_priv_key.export_pk()?.to_vec(),
        pre_hash: hash.to_vec(),
        signature: sign.to_vec(),
        recovery_id: None,
    };

    let payload = res_data::Payload::SignResponse(data);

    return Ok(payload);
}

#[no_mangle]
extern "C" fn wallet_mnemonic_generate_from_display(
    mnemonic_length: usize,
    buffer: *mut c_char,
    len: usize,
) -> bool {
    let mnemonic = match app_mnemonic_generate_rs(mnemonic_length as u32) {
        Ok(v) => v,
        Err(_) => return false,
    };

    let mnemonic = mnemonic.words.join(" ");

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
extern "C" fn wallet_set_pin_cache_from_display(pin: *const c_char) -> bool {
    let bytes = app_pin_text_to_key(pin);

    let res = match bytes {
        Ok(v) => v,
        Err(_) => return false,
    };

    match PIN_CACHE.lock() {
        Ok(mut cache) => {
            cache.copy_from_slice(&res[..32]);
            true
        }
        Err(_) => false,
    }
}

fn wallet_set_pin_cache_from_proto(buffer: Vec<u8>) -> bool {
    if buffer.len() != 32 {
        return false;
    }

    let mut hash = [0u8; 32];
    hash.copy_from_slice(&buffer[..32]);

    let bytes = app_pin_hash_to_key(hash);

    let res = match bytes {
        Ok(v) => v,
        Err(_) => return false,
    };

    match PIN_CACHE.lock() {
        Ok(mut cache) => {
            cache.copy_from_slice(&res[..32]);
            true
        }
        Err(_) => false,
    }
}

#[no_mangle]
extern "C" fn wallet_init_custom_from_display(mnemonic: *const c_char) -> bool {
    let words = unsafe {
        let c_str = core::ffi::CStr::from_ptr(mnemonic);
        c_str.to_str().unwrap_or("").to_string()
    };

    let mnemonic = match mnemonic::Mnemonic::from_phrase(&words) {
        Ok(mnemonic) => mnemonic.to_seed(""),
        Err(_) => return false,
    };

    let seed = match mnemonic {
        Ok(seed) => seed,
        Err(_) => return false,
    };

    let check = storage_seed_write_rs(seed.as_ref(), seed.len());

    if check < 0 {
        return false;
    }

    return true;
}

fn wallet_init_custom_from_proto(mnemonic: String, pin: Vec<u8>) -> Result<res_data::Payload> {
    let bytes = wallet_set_pin_cache_from_proto(pin);

    if !bytes {
        return Err(anyhow!("Set PIN failed"));
    }

    let mnemonic = match mnemonic::Mnemonic::from_phrase(&mnemonic) {
        Ok(mnemonic) => mnemonic,
        Err(_) => return Err(anyhow!("Mnemonic invalid")),
    };

    let seed = match mnemonic.to_seed("") {
        Ok(seed) => seed,
        Err(_) => return Err(anyhow!("Mnemonic invalid")),
    };

    let check = storage_seed_write_rs(seed.as_ref(), seed.len());

    if check < 0 {
        return Err(anyhow!("Storage write failed"));
    }

    let payload = res_data::Payload::InitWalletResponse(proto::InitWalletResponse {
        mnemonic: Some(mnemonic.words.join(" ")),
    });

    lock_mark_unlock();
    return Ok(payload);
}

fn wallet_init_default_from_proto(length: u32, pin: Vec<u8>) -> Result<res_data::Payload> {
    let bytes = wallet_set_pin_cache_from_proto(pin);

    if !bytes {
        return Err(anyhow!("Set PIN failed"));
    }

    let mnemonic = app_mnemonic_generate_rs(length)?;

    let seed = match mnemonic.to_seed("") {
        Ok(seed) => seed,
        Err(_) => return Err(anyhow!("Mnemonic invalid")),
    };

    let check = storage_seed_write_rs(seed.as_ref(), seed.len());

    if check < 0 {
        return Err(anyhow!("Storage write failed"));
    }

    let payload = res_data::Payload::InitWalletResponse(proto::InitWalletResponse {
        mnemonic: Some(mnemonic.words.join(" ")),
    });

    lock_mark_unlock();
    return Ok(payload);
}

#[no_mangle]
extern "C" fn wallet_unlock_from_display(pin: *const c_char) -> bool {
    let bytes = wallet_set_pin_cache_from_display(pin);

    if !bytes {
        return false;
    }

    let mut buffer = vec![0u8; 128];

    if storage_seed_read_rs(buffer.as_mut_ptr(), buffer.len()) < 0 {
        return false;
    }

    lock_mark_unlock();
    return true;
}

fn wallet_unlock_from_proto(hash: Vec<u8>) -> bool {
    let bytes = wallet_set_pin_cache_from_proto(hash);

    if !bytes {
        return false;
    }

    let mut buffer = vec![0u8; 128];

    if storage_seed_read_rs(buffer.as_mut_ptr(), buffer.len()) < 0 {
        return false;
    }

    lock_mark_unlock();
    return true;
}

#[no_mangle]
extern "C" fn wallet_sign_eth_from_display() -> bool {
    let res = app_wallet_sign_eth();
    let check = res.is_ok();
    ETH_SIGN_CACHE.clear().ok();
    app_event_sent_res(res);
    check
}
