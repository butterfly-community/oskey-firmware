use crate::{
    app_cs_random, app_uart_tx_push_array, app_version_get, storage_seed_check, storage_seed_read,
    storage_seed_write,
};
use anyhow::{anyhow, Result};
use oskey_bus::proto::{req_data::Payload, ReqData, ResData};
use oskey_bus::Message;

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
            oskey_action::wallet_init_default(data, app_cs_random, storage_seed_write)?
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
