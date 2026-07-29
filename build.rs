fn main() {
    for path in ["Cargo.toml", "src/lib.rs", "src/rs", "lib/core/action/src"] {
        println!("cargo:rerun-if-changed={path}");
    }

    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let mut config = cbindgen::Config::default();
    config.language = cbindgen::Language::C;
    config.include_guard = Some("OSKEY_BINDINGS_H".into());
    config.enumeration.prefix_with_name = true;
    config.export.include = vec!["AppDisplayAction".into()];
    config.parse.parse_deps = true;
    config.parse.include = Some(vec!["oskey-action".into()]);

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("src/bindings.h");
}
