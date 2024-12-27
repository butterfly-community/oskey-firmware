use std::io::Result;
use std::path::PathBuf;

fn main() -> Result<()> {
    let out = PathBuf::from("src/proto");
    println!("cargo:rerun-if-changed=src/proto/ohw.proto");
    prost_build::Config::new()
        .out_dir(out)
        .compile_protos(&["src/proto/ohw.proto"], &["src/proto"])?;
    Ok(())
}
