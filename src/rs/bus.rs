extern crate alloc;

use crate::rs::wallet::wallet_handle_hub;
use crate::rs::warpper::app_uart_tx_push_array;
use alloc::format;
use anyhow::{anyhow, Result};
use oskey_bus::proto::{res_data, ReqData, ResData};
use oskey_bus::{FrameParser, Message};
use zephyr::sync::SpinMutex;

static APP_UART_REQ_PARSER: AppDataParser = AppDataParser::new();

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

#[no_mangle]
extern "C" fn app_uart_event_rs(data: *const u8, len: usize) -> bool {
    return APP_UART_REQ_PARSER.push_check(unsafe { core::slice::from_raw_parts(data, len) });
}

#[no_mangle]
extern "C" fn app_event_bytes_handle() {
    let _event = app_event_trans();
    return;
}

// Not irq, should be called in main loop or work thread
pub fn app_event_trans() -> Result<()> {
    let test = APP_UART_REQ_PARSER.unpack();
    let req_data = test.ok_or(anyhow!("Waiting"))??;
    let payload = wallet_handle_hub(req_data);
    app_event_sent_res(payload);
    Ok(())
}

pub fn app_event_sent_res(payload: Result<res_data::Payload>) {
    let data = payload.unwrap_or_else(|e| {
        #[cfg(debug_assertions)]
        let error_msg = format!("{:?}", e);
        #[cfg(not(debug_assertions))]
        let error_msg = format!("{:#}", e);
        res_data::Payload::ErrorResponse(oskey_bus::proto::ErrorResponse {
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
