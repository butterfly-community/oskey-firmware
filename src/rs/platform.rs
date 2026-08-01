use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;
use anyhow::{anyhow, Result};
use core::ffi::{c_char, CStr};
use core::fmt::Write;
use oskey_action::proto::{self, res_data};
use oskey_action::{AppMessageSource, FrameParser, Message, WalletOutput, WalletPlatform};

use crate::rs::ffi::{
    app_check_feature, app_check_storage, app_confirmation_complete, app_confirmation_prompt,
    app_csrand_get, app_display_message, app_fido2_reply, app_get_chip_model, app_get_device_id,
    app_get_eui64, app_restart, app_storage_reset, app_uart_send, app_version_get, oskey_bt_send,
    storage_general_check, storage_general_read, storage_general_write, storage_ids,
    AppConfirmationKind, AppConfirmationView, AppSlice,
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
                    let (action, error, value, text) = match payload {
                        res_data::Payload::DisplayResponse(response) => {
                            let Ok(action) = proto::DisplayAction::try_from(response.action) else {
                                continue;
                            };
                            if action == proto::DisplayAction::Unspecified {
                                continue;
                            }
                            (
                                action,
                                proto::AppError::try_from(response.error)
                                    .unwrap_or(proto::AppError::Unspecified),
                                response.value,
                                response.text,
                            )
                        }
                        _ => continue,
                    };

                    unsafe {
                        app_display_message(action, error, value, text.as_ptr(), text.len());
                    }
                }
                AppMessageSource::Fido2 => match output.response.payload {
                    Some(res_data::Payload::Fido2Response(response)) => unsafe {
                        app_fido2_reply(
                            true,
                            response.credential_id.as_ptr(),
                            response.credential_id.len(),
                            response.data.as_ptr(),
                            response.data.len(),
                        );
                    },
                    Some(res_data::Payload::ConfirmationResult(response)) => unsafe {
                        app_confirmation_complete(response.approved);
                    },
                    _ => unsafe {
                        app_fido2_reply(false, core::ptr::null(), 0, core::ptr::null(), 0);
                    },
                },
                AppMessageSource::Confirmation => {
                    let Some(res_data::Payload::ConfirmationPrompt(prompt)) =
                        output.response.payload
                    else {
                        continue;
                    };

                    if !prompt.active {
                        unsafe {
                            app_confirmation_prompt(false, core::ptr::null());
                        }
                        continue;
                    }

                    let Some(content) = prompt.content else {
                        continue;
                    };
                    let mut view = match &content {
                        proto::confirmation_prompt::Content::EthMessage(_) => {
                            AppConfirmationView::new(AppConfirmationKind::EthMessage)
                        }
                        proto::confirmation_prompt::Content::EthTransaction(_) => {
                            AppConfirmationView::new(AppConfirmationKind::EthTransaction)
                        }
                        proto::confirmation_prompt::Content::Fido(_) => {
                            AppConfirmationView::new(AppConfirmationKind::Fido)
                        }
                    };
                    match &content {
                        proto::confirmation_prompt::Content::EthMessage(confirmation) => {
                            view.truncated = confirmation.truncated;
                            view.message_length = confirmation.byte_length;
                            view.preview = AppSlice::new(confirmation.preview.as_bytes());
                            view.signing_hash = AppSlice::new(&confirmation.signing_hash);
                        }
                        proto::confirmation_prompt::Content::EthTransaction(confirmation) => {
                            view.contract_creation = confirmation.contract_creation;
                            view.chain_id = confirmation.chain_id;
                            view.nonce = confirmation.nonce;
                            view.gas_limit = confirmation.gas_limit;
                            view.input_length = confirmation.input_length;
                            view.gas_price = AppSlice::new(confirmation.gas_price.as_bytes());
                            view.to = AppSlice::new(&confirmation.to);
                            view.value = AppSlice::new(confirmation.value.as_bytes());
                            view.selector = AppSlice::new(&confirmation.selector);
                            view.input_hash = AppSlice::new(&confirmation.input_hash);
                            view.signing_hash = AppSlice::new(&confirmation.signing_hash);
                        }
                        proto::confirmation_prompt::Content::Fido(confirmation) => {
                            view.operation = proto::FidoOperation::try_from(confirmation.operation)
                                .unwrap_or(proto::FidoOperation::Unspecified);
                            view.account_is_text = confirmation.account_is_text;
                            view.rp_id = AppSlice::new(confirmation.rp_id.as_bytes());
                            view.account = AppSlice::new(&confirmation.account);
                        }
                    }
                    unsafe {
                        app_confirmation_prompt(true, &view);
                    }
                }
            }
        }
    }
}
