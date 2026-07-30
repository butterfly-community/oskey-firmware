fn main() {
    println!("cargo:rerun-if-changed=src/rs");

    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let mut config = cbindgen::Config::default();
    config.language = cbindgen::Language::C;
    config.include_guard = Some("OSKEY_BINDINGS_H".into());
    config.enumeration.prefix_with_name = true;
    config.export.include = vec![
        "DisplayAction".into(),
        "AppError".into(),
        "ConfirmationKind".into(),
    ];
    config.parse.parse_deps = true;
    config.parse.include = Some(vec!["oskey-action".into()]);

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        // cbindgen cannot discover the re-exported proto types, so include the generated source.
        .with_src("lib/core/bus/src/proto/oskey.rs")
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("src/bindings.h");
}
