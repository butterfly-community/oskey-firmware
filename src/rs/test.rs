// use alloc::vec;
// use ohw_protocol::Message;
// use zephyr::printkln;

// use crate::rs::wallet::{rust_cs_random_vec, wallet_init_default, wallet_sign_msg};

#[no_mangle]
extern "C" fn test_wallet() {
    // printkln!("\n1.Generate Hardware secure random number:\n");
    // printkln!("128 Bit - 256 Bit, Add checksum to 11 bit split. Supports 12, 15, 21, and 24 mnemonics. This use 128 Bit.");

    // let random = rust_cs_random_vec(16);
    // printkln!(
    //     "\nRandom: {} \n\n",
    //     ohw_wallets::alg::hex::encode(random.clone())
    // );

    // printkln!("\n2.Use random entropy generate mnemonic:");
    // let mnemonic = ohw_wallets::mnemonic::Mnemonic::from_entropy(&random).unwrap();
    // printkln!("\nMnemonic: {} \n\n", mnemonic.words.join(" ").as_str());

    // printkln!("\n3.Mnemonic to seed. Supports mnemonic passwords, here the password is ohw.\n");
    // printkln!(
    //     "Key: {} \n\n",
    //     ohw_wallets::alg::hex::encode(mnemonic.clone().to_seed("ohw").unwrap())
    // );

    // printkln!("\n4.BIP32 Root Key:\n");
    // let root = ohw_wallets::wallets::ExtendedPrivKey::derive(
    //     &mnemonic.to_seed("ohw").unwrap(),
    //     "m".parse().unwrap(),
    // )
    // .unwrap();
    // printkln!("Key: {} \n\n", root.encode(false).unwrap());

    // printkln!("\n5.BIP44 ETH Extended Private Key, m/44'/60'/0'/0 Derivation Path:\n");
    // let root = ohw_wallets::wallets::ExtendedPrivKey::derive(
    //     &mnemonic.to_seed("ohw").unwrap(),
    //     "m/44'/60'/0'/0".parse().unwrap(),
    // )
    // .unwrap();
    // printkln!("Key: {} \n\n", root.encode(false).unwrap());

    // printkln!("\n6.ETH Account 0, m/44'/60'/0'/0/0 Derivation Path:\n");
    // let root = ohw_wallets::wallets::ExtendedPrivKey::derive(
    //     &mnemonic.to_seed("ohw").unwrap(),
    //     "m/44'/60'/0'/0/0".parse().unwrap(),
    // )
    // .unwrap();
    // printkln!(
    //     "Key: {} \n\n",
    //     ohw_wallets::alg::hex::encode(root.secret_key)
    // );

    // printkln!("\n7.Test proto");
    // let payload = ohw_protocol::proto::res_data::Payload::VersionResponse(
    //     ohw_protocol::proto::VersionResponse {
    //         version: "1.0.0".into(),
    //         features: Some(ohw_protocol::proto::Features {
    //             initialized: true,
    //             has_hardware_random: true,
    //         }),
    //     },
    // );
    // let response = ohw_protocol::proto::ResData {
    //     payload: payload.into(),
    // };
    // let frame = ohw_protocol::FrameParser::pack(&response.encode_to_vec());
    // printkln!("{:?}", frame);

    // printkln!("\n8.Test Wallet init");
    // let test = wallet_init_default(12, "".into(), None);
    // if test.is_ok() {
    //     printkln!("\nWallet init success");
    // } else {
    //     printkln!("\nWallet init failed");
    // }

    // printkln!("\n9. Test Wallet Sign");

    // let data = vec![0u8; 32];

    // wallet_sign_msg("m/44'/60'/0'/0/0".into(), data, 0).unwrap();

    // printkln!("\nWallet sign success");
}
