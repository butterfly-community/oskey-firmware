extern crate alloc;

use alloc::string::String;
use alloc::string::ToString;
use anyhow::{anyhow, Result};
use oskey_bus::proto;
use oskey_bus::proto::res_data;
use oskey_chain::eth::OSKeyTxEip2930;
use zephyr::sync::SpinMutex;

use crate::rs::wallet::OSKeyHWCallbacks;

pub static ETH_SIGN_CACHE: EthSignRequestCache = EthSignRequestCache::new();

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
        let request = self
            .peek()?
            .ok_or_else(|| anyhow!("No sign request found"))?;
        let tx = request
            .tx
            .ok_or_else(|| anyhow!("Transaction data is missing"))?;

        let text = match tx {
            proto::sign_eth_request::Tx::Eip2930(tx_eip2930) => {
                OSKeyTxEip2930::from_proto(tx_eip2930)?.to_string()
            }
            proto::sign_eth_request::Tx::Eip191(msg_sign) => {
                if msg_sign.message.len() > 512 {
                    msg_sign.message[..512].to_string() + "..."
                } else {
                    msg_sign.message
                }
            }
        };

        Ok(text + "\0")
    }

    pub fn sign(&self) -> Result<res_data::Payload> {
        let callbacks = OSKeyHWCallbacks;

        let cache = self.peek()?;
        let data = cache.ok_or_else(|| anyhow!("No sign request found"))?;

        let res = oskey_action::handle_sign_eth(data, &callbacks)?;

        self.clear()?;

        Ok(res)
    }
}
