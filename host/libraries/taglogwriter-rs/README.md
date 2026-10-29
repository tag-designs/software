---
type: readme
status: current
summary: taglogwriter, a safe Rust wrapper that writes tag SQLite logs through the C++ writer in tagcore_c - building and linking it (shared or static), its API, and the log-import check.
---

# taglogwriter (Rust)

A safe Rust interface to the C++ SQLite log writer. `LogWriter` wraps the
`tlw_*` functions of [`tagcore_c`](../tagcore-c/README.md). It encodes each
message with prost and passes the bytes, so the database is exactly the one
the C++ tools write. The protobuf types come from
[`tagcore-rs`](../tagcore-rs/README.md). The Rust
[`tag-dwnld`](../../commandline-rs/README.md) is built on it. The
[architecture](../tagcore-c/design/architecture.md) covers both crates and
the library together.

## Building

The crate links a library that CMake builds, through
[`tagcore-c-sys`](../tagcore-c-sys/README.md), so `cargo build` alone is not
enough:

```sh
cmake -DBUILD_TAGCORE_C=ON <build-dir>
cmake --build <build-dir> --target tagcore_c_static tagcore_c
export TAGCORE_C_LIB_DIR=<build-dir>/Release/lib
cargo build                          # macOS: libtagcore_c.a; Linux, Windows: shared
TAGCORE_C_LINK=dylib cargo build     # force the shared library
```

**The link follows the platform's packaging** (owner's decision, 2026-10-27). macOS links
statically: each application is its own bundle, so a shared library would be
copied into every one, and a static executable needs nothing at run time.
Linux and Windows link the shared library: the tools share one install
directory, so one copy serves them all. With the shared library, a binary
finds it through an rpath. That rpath is
set by `tagcore-c-sys`'s build script for this crate's own examples and
tests. For a dependent binary, its own package's build script sets it, from
`DEP_TAGCORE_C_LIB_DIR`, which Cargo gives to direct dependents of
`tagcore-c-sys`; see `commandline-rs/build.rs`. On Windows the DLL
must sit beside the executable or on `PATH`.

## Using it

`LogWriter::create(path, config, replace)`, then `write_header(&LogHeader)`,
`begin_log()`, one `write_log(&Ack)` per page, and `end_log()`. `write_log`
returns how many records the page consumed, which advances the index of the
next page to request, and 0 at the end of the log. Only the writer can say
either, since both depend on the tag type. `set_debug()` sets the C++
writer's logging, as `-d` does for the C++ tools. The rustdoc on `src/lib.rs`
has the details.

## Checking

`examples/log-import.rs` repeats `tag-log-import` through the wrapper, with
the same calls in the same order:

```sh
cargo run --release --example log-import -- download.jsonl rust.db3
tag-log-import download.jsonl -o cpp.db3
cmp rust.db3 cpp.db3            # identical, given the same input path
```

The input path matters because both tools record it in the `import_file`
info row.
