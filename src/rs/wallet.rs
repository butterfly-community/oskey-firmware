use crate::app_uart_tx_push_array;
use crate::cs_random;
use alloc::{string::String, vec::Vec};
use anyhow::{anyhow, Result};
use ohw_protocol::proto::DerivePublicKeyResponse;
use ohw_protocol::proto::SignResponse;
use ohw_protocol::{
    proto::{
        req_data, res_data, Features, InitWalletResponse, ReqData, ResData, Unknown,
        VersionResponse,
    },
    FrameParser, Message,
};

static mut GLOBAL_FAKE_STORAGE: Option<[u8; 64]> = None;

pub fn rust_cs_random_vec(len: usize) -> Vec<u8> {
    let mut buffer = Vec::with_capacity(len);
    unsafe {
        buffer.set_len(len);
        cs_random(buffer.as_mut_ptr(), len);
    }
    buffer
}

pub fn event_parser(bytes: &[u8]) -> Result<()> {
    let parser = ohw_protocol::FrameParser::unpack(bytes)?;

    let payload_bytes = parser.ok_or(anyhow!("Waiting"))?;

    let req_data = ReqData::decode(payload_bytes.as_slice()).map_err(|e| anyhow!(e))?;

    let _res = event_hub(req_data);

    Ok(())
}

pub fn event_hub(req: ReqData) -> Result<()> {
    let payload = match req.payload.ok_or(anyhow!("Fail"))? {
        req_data::Payload::Unknown(_unknown) => wallet_unknown_req(),
        req_data::Payload::VersionRequest(_) => wallet_version_req(),
        req_data::Payload::InitRequest(data) => {
            wallet_init_default(data.length, data.password, data.seed)?
        }
        req_data::Payload::InitCustomRequest(data) => {
            wallet_init_custom(data.words, data.password)?
        }
        req_data::Payload::DerivePublicKeyRequest(data) => {
            wallet_drive_public_key(data.path)?
        }
        req_data::Payload::SignRequest(data) => {
            wallet_sign_msg(data.path, data.message, data.id)?
        },
    };

    let response = ResData {
        payload: payload.into(),
    };

    let bytes = response.encode_to_vec();

    let pack = FrameParser::pack(&bytes);

    unsafe {
        app_uart_tx_push_array(pack.as_ptr(), pack.len());
    }

    Ok(())
}

pub fn wallet_unknown_req() -> res_data::Payload {
    let payload = res_data::Payload::Unknown(Unknown {});

    return payload;
}

pub fn wallet_version_req() -> res_data::Payload {
    let features = Features {
        initialized: wallet_storage_check(),
        has_hardware_random: true,
    };

    let version = VersionResponse {
        version: "0.0.1".into(),
        features: features.into(),
    };
    let payload = res_data::Payload::VersionResponse(version);
    return payload;
}

pub fn wallet_storage_save(data: &[u8; 64]) {
    unsafe {
        GLOBAL_FAKE_STORAGE = Some(*data);
    }
}

pub fn wallet_storage_get() -> Option<[u8; 64]> {
    unsafe { GLOBAL_FAKE_STORAGE }
}

pub fn wallet_storage_check() -> bool {
    unsafe { GLOBAL_FAKE_STORAGE.is_some() }
}

pub fn wallet_init_default(
    length: u32,
    password: String,
    seed: Option<Vec<u8>>,
) -> Result<res_data::Payload> {
    let mut random = rust_cs_random_vec((length * 4 / 3).try_into()?);
    if let Some(s) = seed {
        random[..s.len()].clone_from_slice(&s);
    }
    let mnemonic = ohw_wallets::mnemonic::Mnemonic::from_entropy(&random)?;

    wallet_init_save_seed(password, mnemonic.clone())?;

    //TODO: only debug return mnemonic msg.
    let init = InitWalletResponse {
        mnemonic: mnemonic.words.join(" ").into(),
    };

    let payload = res_data::Payload::InitWalletResponse(init);

    return Ok(payload);
}

pub fn wallet_init_custom(words: String, password: String) -> Result<res_data::Payload> {
    let mnemonic = ohw_wallets::mnemonic::Mnemonic::from_phrase(&words)?;

    wallet_init_save_seed(password, mnemonic.clone())?;

    //TODO: only debug return mnemonic msg.
    let init = InitWalletResponse {
        mnemonic: mnemonic.words.join(" ").into(),
    };

    let payload = res_data::Payload::InitWalletResponse(init);

    return Ok(payload);
}

pub fn wallet_init_save_seed(
    password: String,
    mnemonic: ohw_wallets::mnemonic::Mnemonic,
) -> Result<()> {
    if wallet_storage_check() {
        return Err(anyhow!("Wallet already initialized"));
    }
    let seed = if password.is_empty() {
        mnemonic.to_seed("")?
    } else {
        mnemonic.to_seed(&password)?
    };
    wallet_storage_save(&seed);

    Ok(())
}

pub fn wallet_drive_public_key(path: String) -> Result<res_data::Payload> {
    let seed = wallet_storage_get().ok_or(anyhow!("Wallet not initialized"))?;

    let ex_priv_key = ohw_wallets::wallets::ExtendedPrivKey::derive(&seed, path.parse()?)?;

    let pk = ex_priv_key.export_pk()?;

    let data = DerivePublicKeyResponse {
        path,
        public_key: pk.into(),
    };

    let payload = res_data::Payload::DerivePublicKeyResponse(data);

    return Ok(payload);
}

pub fn wallet_sign_msg(path: String, msg: Vec<u8>, id: i32) -> Result<res_data::Payload> {

    let seed = wallet_storage_get().ok_or(anyhow!("Wallet not initialized"))?;

    let ex_priv_key = ohw_wallets::wallets::ExtendedPrivKey::derive(&seed, path.parse()?)?;

    let sign = ex_priv_key.sign(msg.as_slice())?;

    let data = SignResponse {
        id,
        signature: sign.into(),
        sk: Some(ex_priv_key.secret_key.into())
    };

    let payload = res_data::Payload::SignResponse(data);

    return Ok(payload);
}
