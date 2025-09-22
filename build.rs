use std::env;

fn main() {
    let target = env::var("TARGET").unwrap_or_default();

    println!("cargo:rerun-if-changed=./src/**/*.rs");

    if target == "x86_64-unknown-none" {
        let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
        cbindgen::Builder::new()
            .with_crate(crate_dir)
            .with_language(cbindgen::Language::C)
            .generate()
            .expect("Unable to generate bindings")
            .write_to_file("src/bindings.h");
    }
}
