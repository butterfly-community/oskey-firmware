use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;
use anyhow::{anyhow, Result};
use core::ffi::{c_char, CStr};
use core::fmt::Write;
use oskey_action::proto::{self, res_data};
use oskey_action::{AppMessageSource, FrameParser, Message, WalletOutput, WalletPlatform};

use crate::rs::ffi::{
    app_check_feature, app_check_storage, app_csrand_get, app_display_message, app_get_chip_model,
    app_get_device_id, app_get_eui64, app_restart, app_storage_reset, app_uart_send,
    app_version_get, oskey_bt_send, storage_general_check, storage_general_read,
    storage_general_write, storage_ids, user_button_request, AppDisplayAction,
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

    fn storage_ready(&self) -> bool {
        unsafe { app_check_storage() }
    }

    fn seed_exists(&self) -> bool {
        unsafe { storage_general_check(storage_ids.seed) }
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

    fn reset_storage(&self) {
        unsafe { app_storage_reset() };
    }

    fn restart(&self) {
        unsafe { app_restart() };
    }
}

impl Platform {
    pub(crate) fn output(outputs: Vec<WalletOutput>) {
        for output in outputs {
            match output.target {
                AppMessageSource::Uart | AppMessageSource::Bluetooth => {
                    let encoded = output.response.encode_to_vec();
                    let frame = FrameParser::pack(&encoded);
                    if output.target == AppMessageSource::Bluetooth {
                        unsafe {
                            oskey_bt_send(frame.as_ptr(), frame.len());
                        }
                    } else {
                        unsafe {
                            app_uart_send(frame.as_ptr(), frame.len());
                        }
                    }
                }
                AppMessageSource::Display => {
                    let Some(payload) = output.response.payload else {
                        continue;
                    };
                    let (action, text) = match payload {
                        res_data::Payload::DisplayResponse(response) => {
                            let action = match proto::DisplayAction::try_from(response.action) {
                                Ok(proto::DisplayAction::Ready) => AppDisplayAction::Ready,
                                Ok(proto::DisplayAction::Mnemonic) => AppDisplayAction::Mnemonic,
                                Ok(proto::DisplayAction::Sign) => AppDisplayAction::Sign,
                                Ok(proto::DisplayAction::Error) => AppDisplayAction::Error,
                                _ => continue,
                            };
                            (action, response.text)
                        }
                        _ => continue,
                    };

                    unsafe {
                        app_display_message(action, text.as_ptr(), text.len());
                    }
                }
                AppMessageSource::Button => {
                    let Some(res_data::Payload::UserActionPrompt(prompt)) = output.response.payload
                    else {
                        continue;
                    };
                    unsafe {
                        user_button_request(prompt.active);
                    }
                }
            }
        }
    }
}
