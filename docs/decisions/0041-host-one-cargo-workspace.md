---
type: decision
status: accepted
summary: All of the host's Rust crates form one Cargo workspace at host/Cargo.toml with one Cargo.lock and, in a CMake build, one shared cargo target directory, replacing six independent lock files and four application workspaces; the crates' directories and names are unchanged.
---

# 0041. Host: one Cargo workspace for the Rust code

Date: 2026-10-31

## Context

Between 2026-10-26 and 2026-10-30 the host gained fifteen Rust crates in ten
days ([0035](0035-host-tagprogram-rs.md), [0037](0037-host-tagmonitor-rs.md),
[0039](0039-host-tagcalibrate-rs.md), tag-flash). Each library crate and the
command-line crate was its own Cargo root with its own `Cargo.lock`, and each
application was a two-crate workspace with another. There were seven lock
files to keep in step, seven `target/` directories, and no single place from
which `cargo test`, `cargo doc` or `cargo clippy` covered the Rust code.
rust-analyzer, opened on the repository, saw none of it as one project. The
CMake build ran cargo once per application with a separate target directory
each, so Tauri, probe-rs and the tag interface were compiled four times per
build tree, and CI cached four workspaces.

The owner asked for the code base to be made easier to navigate and
understand for an engineer new to Rust.

## Decision

- **One workspace, `host/Cargo.toml`**, with every host Rust crate as a
  member: the six library crates, `commandline-rs`, and each application's
  `core` and `app` crates. The two spikes are excluded and keep their own
  roots.
- **One lock file, `host/Cargo.lock`**, seeded from Tag Programmer's (the
  largest) so that no dependency's version changed, and completed from the
  local registry cache. Release builds keep `--locked`.
- **Directories and crate names do not change.** The `-rs` suffix stays
  where a C++ directory of the same name exists; the README's table maps
  directory to crate to executable.
- **One cargo target directory per CMake build tree**, `<build>/host/cargo`,
  and one definition of the application targets, `host_rust_app()` in
  `cmake/HostRust.cmake`, which the four applications and the flash tools
  use. cargo serialises concurrent invocations on the directory itself.
- **One rustdoc run** for the developer portal's Rust reference, over the
  workspace, instead of one per former workspace.

## Alternatives considered

- **Leave the crates separate and document the layout.** The map would have
  been enough to find things, but every routine check would still be run
  from seven places, and an IDE would still show seven projects.
- **Rename the directories to drop `-rs`.** Every path in CMake, CI, the
  notices tooling and some two hundred documentation links would change,
  for a cosmetic gain; `tagcore-rs` beside `tagcore` also says plainly which
  is the port.
- **Keep per-application target directories in CMake.** Simpler to leave,
  but it kept the fourfold compilation and four cache entries; with one
  workspace the shared directory is the natural layout.
- **Move the crates under one `host/rust/` tree.** It would separate each
  Rust application from the Qt application it ports and whose test oracles
  it reads, and break the pattern that a library and its port sit side by
  side.

## Evidence

- `cargo check --workspace --all-targets --locked --offline` passes for
  every crate, tests and examples included, with the C library absent
  (`TAGCORE_C_LINK=dylib` against an empty directory, as the portal's
  rustdoc target does).
- `cargo test --locked` passes for the crates that need no C library
  (`tagcore`, `tag-tools`, `tagprobe`, `tagprogram-core`,
  `tagmonitor-core`).
- The merged lock added entries only; `git diff` shows no version changed.
- `tagprogram` and `flash_tools` build through `host_rust_app()` and
  `rust_flash_tools()` into the shared target directory from a harness that
  includes the real `CMakeLists.txt` files.
- The four third-party notices files were regenerated from the one lock.
  Feature unification across the workspace makes `cargo metadata` report a
  few more crates reachable from each application than before (eleven for
  Tag View); the generator already errs toward listing too much, and the
  list is a superset of what each binary contains.

## Consequences

- The CMake targets, the CI cache, the embed check in the release workflow,
  the notices tooling and the bench scripts' fallback path
  (`host/target/release`) all name the workspace's locations.
- A dependency change edits `host/Cargo.lock` once, and all four notices
  files are regenerated from it.
- A `cargo build --workspace` without `TAGCORE_C_LIB_DIR` fails in
  `tagcore-c-sys`'s build script with the message saying how to build the
  library; the crates that do not link it build with `-p`.
- The release workflow has not yet run on this layout; the first release
  after it should check the Rust steps.
