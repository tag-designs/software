//! Generates the protobuf message types and embeds the tags' default configs.
//!
//! `tag.proto` imports `tagdata.proto`, so compiling it yields every message
//! a monitor RPC can carry. Neither file declares a package, so prost writes
//! them to `_.rs` in `OUT_DIR`; `src/proto.rs` includes that file. Every
//! message also derives `prost_reflect::ReflectMessage` against the
//! descriptor pool written next to it, which is what the text-format, JSON
//! and ConfigShape code reflect over.
//!
//! Each `embedded/proto-c/<name>-proto-c/default-config.json` is embedded, in
//! directory-name order, as `defaultconfigs.rs`, the counterpart of the
//! `defaultconfigs.cc` the C++ build generates for ConfigShape.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};

/// Compiles the protos (with reflection) and embeds the default configs.
///
/// Uses the vendored `protoc` unless `PROTOC` is set. Fails the build if a
/// proto does not compile or `embedded/proto-c` cannot be read.
fn main() -> std::io::Result<()> {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let root = manifest.join("../../..");
    let proto_dir = root.join("proto");
    let tag_proto = proto_dir.join("tag.proto");

    println!("cargo:rerun-if-changed={}", tag_proto.display());
    println!(
        "cargo:rerun-if-changed={}",
        proto_dir.join("tagdata.proto").display()
    );

    // A packaging build has no protoc of its own; use the vendored one
    // unless PROTOC names another.
    if std::env::var_os("PROTOC").is_none() {
        let protoc = protoc_bin_vendored::protoc_bin_path().expect("vendored protoc");
        // SAFETY: build scripts are single-threaded.
        unsafe { std::env::set_var("PROTOC", protoc) };
    }

    prost_reflect_build::Builder::new()
        .descriptor_pool("crate::proto::DESCRIPTOR_POOL")
        .file_descriptor_set_path(out_dir.join("file_descriptor_set.bin"))
        .compile_protos(&[tag_proto], &[proto_dir])?;

    embed_default_configs(&root.join("embedded/proto-c"), &out_dir)
}

/// Writes `defaultconfigs.rs`: a `&[(&str, &str)]` of (directory name,
/// default-config.json text), sorted by directory name.
fn embed_default_configs(proto_c: &Path, out_dir: &Path) -> std::io::Result<()> {
    println!("cargo:rerun-if-changed={}", proto_c.display());
    let mut found: Vec<(String, PathBuf)> = Vec::new();
    for entry in std::fs::read_dir(proto_c)? {
        let entry = entry?;
        let name = entry.file_name().to_string_lossy().into_owned();
        let config = entry.path().join("default-config.json");
        if name.ends_with("-proto-c") && config.is_file() {
            println!("cargo:rerun-if-changed={}", config.display());
            found.push((name, config.canonicalize()?));
        }
    }
    found.sort();

    let mut src = String::from("&[\n");
    for (name, path) in &found {
        writeln!(src, "    ({name:?}, include_str!({:?})),", path.display()).unwrap();
    }
    src.push(']');
    std::fs::write(out_dir.join("defaultconfigs.rs"), src)
}
