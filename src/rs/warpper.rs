use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::c_char;
use core::ffi::c_int;
use core::ffi::CStr;

#[allow(unused_doc_comments)]
/// cbindgen:ignore
extern "C" {
    pub fn app_csrand_get(dst: *mut u8, len: usize) -> bool;
    pub fn app_version_get(data: *mut u8, len: usize) -> bool;
    pub fn app_check_feature(data: *mut u8, len: usize) -> bool;
    pub fn app_check_status(data: *mut u8, len: usize) -> bool;
    pub(crate) fn app_uart_tx_push_array(data: *const u8, len: usize);
    pub(crate) fn app_display_sign(text: *const c_char);
    pub(crate) fn storage_general_check(id: u16) -> bool;
    pub(crate) fn storage_general_read(data: *mut u8, len: usize, id: u16) -> c_int;
    pub(crate) fn storage_general_write(data: *const u8, len: usize, id: u16) -> bool;
}

pub fn app_csrand_get_rs(bytes: &mut [u8]) -> bool {
    unsafe { app_csrand_get(bytes.as_mut_ptr(), bytes.len()) }
}

pub fn app_version_get_rs() -> String {
    let mut buffer = [0u8; 32];
    unsafe {
        app_version_get(buffer.as_mut_ptr(), buffer.len());
    }
    String::from(
        CStr::from_bytes_until_nul(&buffer)
            .unwrap_or(CStr::from_bytes_with_nul(b"unknown\0").unwrap())
            .to_str()
            .unwrap_or("unknown"),
    )
}

#[allow(unused)]
pub enum Status {
    StorageInit = 0,
    Locked = 1,
}

pub fn app_check_status_rs(mask: Status) -> bool {
    let mut buffer = [0u8; 16];

    if !unsafe { app_check_status(buffer.as_mut_ptr(), buffer.len()) } {
        return false;
    }

    let index = mask as usize;
    index < buffer.len() && buffer[index] != 0
}

#[allow(unused)]
pub enum Feature {
    SecureBoot = 0,
    FlashEncryption = 1,
    Bootloader = 2,
    StorageInit = 3,
    HardwareRng = 4,
    DisplayInput = 5,
    UserKey = 6,
}

pub fn app_check_feature_rs(mask: Feature) -> bool {
    let mut buffer = [0u8; 16];

    if !unsafe { app_check_feature(buffer.as_mut_ptr(), buffer.len()) } {
        return false;
    }

    let index = mask as usize;
    index < buffer.len() && buffer[index] != 0
}

pub fn app_check_status_vec() -> Vec<u8> {
    let mut buffer = [0u8; 16];

    if unsafe { app_check_status(buffer.as_mut_ptr(), buffer.len()) } {
        buffer.to_vec()
    } else {
        Vec::new()
    }
}

pub fn app_check_feature_vec() -> Vec<u8> {
    let mut buffer = [0u8; 16];

    if unsafe { app_check_feature(buffer.as_mut_ptr(), buffer.len()) } {
        buffer.to_vec()
    } else {
        Vec::new()
    }
}
