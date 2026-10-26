//! Generates the protobuf message types from the shared `proto/` directory.
//!
//! `tag.proto` imports `tagdata.proto`, so compiling it yields every message
//! a monitor RPC can carry. Neither file declares a package, so prost writes
//! them to `_.rs` in `OUT_DIR`; `src/proto.rs` includes that file.

use std::path::PathBuf;

fn main() -> std::io::Result<()> {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let proto_dir = manifest.join("../../../proto");
    let tag_proto = proto_dir.join("tag.proto");

    println!("cargo:rerun-if-changed={}", tag_proto.display());
    println!(
        "cargo:rerun-if-changed={}",
        proto_dir.join("tagdata.proto").display()
    );

    prost_build::compile_protos(&[tag_proto], &[proto_dir])
}
