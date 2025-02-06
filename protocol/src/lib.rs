#![no_std]
extern crate alloc;
use alloc::vec::Vec;

use anyhow::Result;
pub use prost::Message;

pub mod proto {
    include!("proto/ohw.rs");
}

pub struct FrameParser {}

impl FrameParser {
    const MAGIC: &'static [u8] = "₿".as_bytes();
    const HEADER_LEN: usize = Self::MAGIC.len() + 2;

    pub fn unpack(buffer: &[u8]) -> Result<Option<Vec<u8>>> {
        if !buffer.starts_with(Self::MAGIC) {
            anyhow::bail!("Magic header fail!");
        }

        if buffer.len() < Self::HEADER_LEN {
           return Ok(None);
        }

        let payload_len = u16::from_be_bytes([buffer[3], buffer[4]]) as usize;

        if buffer.len() < Self::HEADER_LEN + payload_len {
            return Ok(None);
        }

        Ok(Some(
            buffer[Self::HEADER_LEN..Self::HEADER_LEN + payload_len].to_vec(),
        ))
    }

    pub fn pack(data: &[u8]) -> Vec<u8> {
        let mut frame = Vec::with_capacity(Self::HEADER_LEN + data.len());
        frame.extend_from_slice(Self::MAGIC);
        frame.extend_from_slice(&(data.len() as u16).to_be_bytes());
        frame.extend_from_slice(data);
        frame
    }
}

#[cfg(test)]
mod test {
    extern crate alloc;
    use super::*;
    use anyhow::{anyhow, Ok, Result};
    use prost::Message;
    use proto::{res_data::Payload, Features, ResData, VersionResponse};

    fn get_test_payload_bytes() -> Vec<u8> {
        let payload = Payload::VersionResponse(VersionResponse {
            version: "1.0.0".into(),
            features: { Features::default() }.into(),
        });

        let response = ResData {
            payload: payload.into(),
        };

        let bytes = response.encode_to_vec();

        return bytes;
    }


    #[test]
    fn test_version() -> Result<()> {
        let bytes = get_test_payload_bytes();

        let decoded_response = proto::ResData::decode(bytes.as_slice()).map_err(|e| anyhow!(e))?;

        if let Payload::VersionResponse(resp) =
            decoded_response.payload.ok_or(anyhow!("Decode Error"))?
        {
            assert_eq!(resp.version, "1.0.0");
        } else {
            panic!("Expected VersionResponse");
        }

        Ok(())
    }

    #[test]
    fn test_frame() -> Result<()> {
        let bytes = get_test_payload_bytes();

        let frame = FrameParser::pack(&bytes);
        let payload = FrameParser::unpack(&frame)?.unwrap();

        assert_eq!(payload, bytes);

        let mut invalid_header = frame.clone();
        invalid_header[0] = b'x';
        assert!(FrameParser::unpack(&invalid_header).is_err());

        let short_frame = &frame.clone()[..frame.len() - 1];
        assert!(FrameParser::unpack(short_frame).is_ok());

        Ok(())
    }

    #[test]
    fn test_frame_with_proto() -> Result<()> {
        let bytes = get_test_payload_bytes();

        let frame = FrameParser::pack(&bytes);

        let data = FrameParser::unpack(&frame)?.unwrap();

        let decoded_msg = ResData::decode(data.as_slice()).map_err(|e| anyhow!(e))?;

        if let Some(Payload::VersionResponse(resp)) = decoded_msg.payload {
            assert_eq!(resp.version, "1.0.0");
        } else {
            anyhow::bail!("Payload mismatch");
        }

        Ok(())
    }
}
