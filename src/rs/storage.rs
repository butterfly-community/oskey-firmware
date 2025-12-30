extern crate alloc;
extern crate zephyr;

use alloc::string::ToString;
use alloc::vec;
use alloc::vec::Vec;
use anyhow::Result;
use core::ffi::c_char;
use core::ffi::c_int;
use oskey_wallet::alg::crypto;
use zephyr::sync::SpinMutex;

use crate::rs::warpper::app_csrand_get_rs;
use crate::rs::warpper::storage_general_read;
use crate::rs::warpper::storage_general_write;

#[no_mangle]
pub static STORAGE_ID_SEED: u16 = 2;
#[no_mangle]
pub static STORAGE_ID_PIN: u16 = 10;
#[no_mangle]
pub static STORAGE_ID_SALT: u16 = 11;

pub static WALLET_STORAGE: WalletStorage = WalletStorage::new();

pub struct WalletStorage {
    pin_cache: SpinMutex<[u8; 32]>,
    lock_mark: SpinMutex<bool>,
}

#[allow(unused)]
impl WalletStorage {
    pub const fn new() -> Self {
        Self {
            pin_cache: SpinMutex::new([0u8; 32]),
            lock_mark: SpinMutex::new(false),
        }
    }

    pub fn get_pin_cache(&self) -> Result<[u8; 32]> {
        Ok(*self.pin_cache.lock()?)
    }

    pub fn get_salt(&self) -> Result<Vec<u8>> {
        let mut buffer = vec![0u8; 32];
        let len = unsafe { storage_general_read(buffer.as_mut_ptr(), buffer.len(), STORAGE_ID_SALT) };

        if len == 32 {
            return Ok(buffer);
        }

        let mut salt = vec![0u8; 32];
        app_csrand_get_rs(&mut salt);

        if unsafe { storage_general_write(salt.as_ptr(), salt.len(), STORAGE_ID_SALT) } {
            Ok(salt)
        } else {
            Err(anyhow::anyhow!("Failed to save salt"))
        }
    }

    pub fn pin_char_to_hash(pin: *const c_char) -> Result<[u8; 32]> {
        let password = unsafe {
            let c_str = core::ffi::CStr::from_ptr(pin);
            c_str.to_str()?.to_string()
        };
        let salt = WALLET_STORAGE.get_salt()?;
        let hash = crypto::Hash::sha256(&[password.as_bytes(), &salt].concat())?;
        Ok(hash)
    }

    pub fn set_pin_from_hash(&self, buffer: Vec<u8>) -> bool {
        if buffer.len() != 32 {
            return false;
        }

        let mut hash = [0u8; 32];
        hash.copy_from_slice(&buffer[..32]);

        let mnemonic = match oskey_wallet::mnemonic::Mnemonic::from_entropy(&hash) {
            Ok(v) => v,
            Err(_) => return false,
        };

        let bytes = match mnemonic.to_seed("OSKey") {
            Ok(v) => v,
            Err(_) => return false,
        };

        match self.pin_cache.lock() {
            Ok(mut cache) => {
                cache.copy_from_slice(&bytes[..32]);
                true
            }
            Err(_) => false,
        }
    }

    pub fn check_status(&self) -> bool {
        self.lock_mark.lock().map(|guard| *guard).unwrap_or(false)
    }

    pub fn set_lock(&self, value: bool) {
        let mut guard = match self.lock_mark.lock() {
            Ok(g) => g,
            Err(_) => return,
        };
        *guard = value;
    }

    pub fn lock(&self) {
        self.set_lock(true);
    }

    pub fn unlock(&self) {
        self.set_lock(false);
    }

    pub fn read_seed(&self, data: *mut u8, len: usize) -> c_int {
        let mut buffer = vec![0u8; 128];

        let check =
            unsafe { storage_general_read(buffer.as_mut_ptr(), buffer.len(), STORAGE_ID_SEED) };
        if check < 0 {
            return check;
        }

        let pin = match self.get_pin_cache() {
            Ok(v) => v,
            Err(_) => return -2000,
        };

        let mut nonce = [0u8; 12];
        nonce.copy_from_slice(&buffer[0..12]);

        let secret = &buffer[12..check as usize];

        let real = match crypto::ChaCha20Poly1305Cipher::decrypt(&pin, &nonce, secret) {
            Ok(v) => v,
            Err(_) => return -2001,
        };

        if real.len() > len {
            return -2002;
        }

        unsafe {
            core::ptr::copy_nonoverlapping(real.as_ptr(), data, real.len());
        }
        real.len() as c_int
    }

    pub fn write_seed(&self, data: &[u8], len: usize) -> c_int {
        let pin = match self.get_pin_cache() {
            Ok(v) => v,
            Err(_) => return -2000,
        };

        let mut nonce = [0u8; 12];
        app_csrand_get_rs(&mut nonce);

        let encrypt = match crypto::ChaCha20Poly1305Cipher::encrypt(&pin, &nonce, &data[..len]) {
            Ok(v) => v,
            Err(_) => return -2000,
        };

        let mut secret = Vec::with_capacity(nonce.len() + encrypt.len());
        secret.extend_from_slice(&nonce);
        secret.extend_from_slice(&encrypt);

        let check =
            unsafe { storage_general_write(secret.as_ptr(), secret.len(), STORAGE_ID_SEED) };
        if !check {
            return -2001;
        }
        0
    }

    pub fn unlock_from_hash(&self, hash: Vec<u8>) -> bool {
        if !self.set_pin_from_hash(hash) {
            return false;
        }

        let mut buffer = vec![0u8; 128];
        if self.read_seed(buffer.as_mut_ptr(), buffer.len()) < 0 {
            return false;
        }

        self.unlock();
        true
    }
}

#[no_mangle]
pub extern "C" fn wallet_check_lock() -> bool {
    WALLET_STORAGE.check_status()
}

#[no_mangle]
pub extern "C" fn wallet_lock() {
    WALLET_STORAGE.lock();
}

#[no_mangle]
pub extern "C" fn wallet_set_pin_cache_from_display(pin: *const c_char) -> bool {
    let hash = match WalletStorage::pin_char_to_hash(pin) {
        Ok(v) => v,
        Err(_) => return false,
    };

    WALLET_STORAGE.set_pin_from_hash(hash.to_vec());
    return true;
}

#[no_mangle]
pub extern "C" fn wallet_unlock_from_display(pin: *const c_char) -> bool {
    let hash = match WalletStorage::pin_char_to_hash(pin) {
        Ok(v) => v,
        Err(_) => return false,
    };
    WALLET_STORAGE.unlock_from_hash(hash.to_vec())
}
