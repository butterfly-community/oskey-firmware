#[allow(clippy::field_reassign_with_default)]
fn main() {
    println!("cargo:rerun-if-changed=src/rs/ffi.rs");
    println!("cargo:rerun-if-changed=lib/core/action/src/lib.rs");
    println!("cargo:rerun-if-changed=lib/core/chain/src/lib.rs");
    println!("cargo:rerun-if-changed=lib/core/protocol/src/proto/oskey.rs");
    println!("cargo:rerun-if-changed=lib/core/protocol/src/proto/oskey.proto");

    let mut config = cbindgen::Config::default();
    config.language = cbindgen::Language::C;
    config.include_guard = Some("OSKEY_BINDINGS_H".into());
    config.enumeration.prefix_with_name = true;
    config.export.include = vec![
        "AppCore".into(),
        "AppCoreCommandKind".into(),
        "AppCoreCommandView".into(),
        "AppCoreEffectKind".into(),
        "AppCoreEffectView".into(),
        "AppConfirmation".into(),
        "AppConfirmationKind".into(),
        "AppSlice".into(),
        "AppError".into(),
        "ConfirmationChoice".into(),
        "ConfirmationOutcome".into(),
        "FidoOperation".into(),
        "FidoRequestKind".into(),
        "FidoStatus".into(),
        "LocalAction".into(),
        "LocalRequestKind".into(),
        "Transport".into(),
        "TransportRoute".into(),
        "WalletState".into(),
    ];
    config.export.exclude = vec!["CREDENTIAL_ID_SIZE".into(), "NONCE_SIZE".into()];
    cbindgen::Builder::new()
        .with_src("src/rs/ffi.rs")
        .with_src("lib/core/action/src/lib.rs")
        .with_src("lib/core/chain/src/lib.rs")
        // cbindgen cannot discover the re-exported proto types, so include the generated source.
        .with_src("lib/core/protocol/src/proto/oskey.rs")
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("src/bindings.h");
}
