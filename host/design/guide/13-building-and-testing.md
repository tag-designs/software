---
type: design
status: current
summary: Chapter 13 of the Host Software Guide - the toolchains, the cargo and CMake commands, the features and environment variables that change what is built, how the packages are assembled per platform, the test strategy (unit tests, oracles against the Qt applications, hardware tests, byte-identical downloads), the documentation checks, and what CI does.
---

# 13. Building and testing

This chapter assumes chapter 2 (the two build systems) and is the one to
keep open while setting up a machine. The commands are for macOS and Linux;
the root `README.md` has the Windows preset and prerequisites.

## Toolchains

| For | Install | Check |
| --- | --- | --- |
| The Rust crates | Rust 1.85 or newer from [rustup](https://rustup.rs); on Linux, `libudev-dev` for the vendored libusb and `libwebkit2gtk-4.1-dev` for the applications | `cargo --version` |
| The C++ libraries, `libtagcore_c`, the C++ tools | CMake 3.20+, a C++20 compiler, protobuf, SQLite 3, libusb-1.0 (with `pkg-config` off Windows), Git; vcpkg on Windows and macOS | `cmake --version` |
| The Qt applications | Qt 6.10.3 (`CMAKE_PREFIX_PATH` or `Qt6_DIR`) | the configure warns on another version |
| The user manual, the portal | Python 3 with `host/docs/requirements.txt`; Doxygen for the C++ reference | |

Nothing else is needed for the Rust side: libusb is built from source by
`rusb`, and `protoc` comes from `protoc-bin-vendored` unless `PROTOC` names
one.

## cargo

From `host/` (the workspace root), or any crate directory within it:

```sh
cargo build -p tagcore                                   # one crate, debug
cargo build --release -p tag-tools                       # the command-line tools, into host/target/release
cargo run -p tagprogram                                  # a Tauri app in debug; needs no C++ library
cargo test -p tagcore -p tag-tools -p tagprobe -p tagprogram-core -p tagmonitor-core
cargo doc --no-deps --document-private-items -p tagcore  # host/target/doc/tagcore/index.html
cargo clippy --all-targets -p tagcore                    # lints, the documentation lints included
```

Crates that link the C++ library (`tagcore-c-sys` and everything above it:
`taglogwriter`, `calibsession`, `sensorviz-session`, `tagcalibrate-core`,
`sensorviz-core`, `tagview`, `tagcalibrate`, Tag Monitor with `sqlitelog`,
`tag-dwnld`) need it built first:

```sh
cmake -S . -B build-host -DBUILD_EMBEDDED=OFF -DBUILD_TAGCORE_C=ON   # or BUILD_RUST_APPS=ON, which implies it
cmake --build build-host --target tagcore_c_static tagcore_c          # macOS: the archive; elsewhere: the shared library
export TAGCORE_C_LIB_DIR=$PWD/build-host/Release/lib                  # or Debug, by configuration
cd host && cargo test -p calibsession -p sensorviz-session -p taglogwriter
cargo run -p tagview -- ../host/docs/fixtures/sensorviz/imutag.db3
```

Without the variable, `cargo build --workspace` stops in `tagcore-c-sys`'s
build script with a message saying this. For `cargo check` and `cargo doc`,
which never link, an empty directory and `TAGCORE_C_LINK=dylib` are enough;
the portal's `rust_docs` target does exactly that.

### Features and environment variables

| Name | Where | Effect |
| --- | --- | --- |
| feature `sqlitelog` | `tag-tools` (for `tag-dwnld`), `tagmonitor-core`, `tagmonitor` | Brings in `taglogwriter` and so the C++ writer; off by default so the other binaries build with cargo alone. CMake always turns it on for Tag Monitor |
| feature `instrumentation` | `tagcore` (default on) | Counts and times USB transfers, ST-LINK commands and RPCs |
| `TAGCORE_C_LIB_DIR` | `tagcore-c-sys` build script | The directory holding `libtagcore_c` |
| `TAGCORE_C_LINK` | `tagcore-c-sys` build script | `static` (macOS default) or `dylib` (Linux and Windows default) |
| `TAGCORE_C_RPATH` | the `app` and `tag-tools` build scripts | The run-time search path written into a binary that links the shared library (`$ORIGIN/../lib` in the packages) |
| `PROTOC` | `tagcore` build script | A `protoc` to use instead of the vendored one |
| `TAGPROGRAM_HOST_VERSION` | `tagprogram-core` (compile time) | The version written into the record CSV; CMake passes the package version |
| `RUST_LOG` | tools and examples with `env_logger` | `debug` shows the attach detail |
| `TAGMONITOR_NO_ATTACH`, `TAGCALIBRATE_NO_ATTACH` | the applications | Do not attach when the window opens |
| `TAGCALIBRATE_REPLAY=<capture.json>` | Tag Calibrator | Replay a capture instead of attaching |
| `SENSORVIZ_PREVIEW_DIALOG=visible\|sides\|colors\|calibration` | Tag View | Open a dialog after loading, for screenshots |
| `CARGO_TARGET_DIR` | cargo | Where output goes; CMake sets `<build>/host/cargo` so a CMake build never writes into `host/target` |

## CMake

The top-level options that matter to the host:

| Option | Default | Builds |
| --- | --- | --- |
| `BUILD_HOST` | on | The C++ libraries and tools; `host/` is not entered without it |
| `BUILD_QT_APPS` | on | The Qt applications and `sensorui` |
| `BUILD_RUST_APPS` | on where `cargo` is found | The four Rust applications, `tag-flash` and `base-flash`, and `libtagcore_c` |
| `BUILD_TAGCORE_C` | off | `libtagcore_c` alone, for developing against it |
| `BUILD_HOST_DOCS` | `BUILD_QT_APPS` | The user manual in the default build and the package |
| `BUILD_DEVELOPER_DOCS` | off | The developer portal (`developer_docs`), with Doxygen and rustdoc |
| `BUILD_TAG_SERVER` | off | `tag-server` (needs CMake 3.28 and a network fetch) |
| `EMBEDDED_PROGRAMMER` | `TAG_FLASH` where available | Which programmer the embedded `-download` targets run |

Focused targets: `tagcore`, `tagcore_c_static`, `tagcore_c`, `qtmonitor`,
`sensorviz`, `tagprogram`, `tagmonitor`, `tagcalibrate`, `tagview`,
`flash_tools`, `docs`, `developer_docs`, `rust_docs`, `api_docs`, `package`.
A Rust target always runs cargo (cargo decides what is stale) with
`--locked`, in the host workspace, through `host_rust_app()` in
`cmake/HostRust.cmake`, and copies the executable to `<build>/<Config>/bin`.

### Packages

`cpack` (the `package` target) builds one package per platform, and the
release workflow builds all three:

| Platform | Package | Rust applications | `libtagcore_c` |
| --- | --- | --- | --- |
| macOS | a signed, notarised DMG of `tag_tools/` with `.app` bundles and plain command-line executables in `bin/` | bundles assembled by CMake (binary, `Info.plist` from `cmake/Info.plist.in`, icon), signed by the same code as the Qt bundles | linked statically into each executable |
| Windows | a ZIP of `tag_tools/` | the real executables in `tag_tools/lib` beside `tagcore_c.dll` and the VC runtime, started by a GUI launcher of the same name in `tag_tools/` (Windows finds DLLs only beside a program) | one DLL in `tag_tools/lib` |
| Linux | a relocatable tarball of `tag_tools/` carrying its own Qt | `tag_tools/bin/<name>`, needing the system's WebKitGTK 4.1 | one `.so` in `tag_tools/lib`, found by `$ORIGIN/../lib` |

Each application's `design/build-and-packaging.md` has the detail; Linux
packaging and its container tests are in
[`host/design/linux-package.md`](../linux-package.md); the macOS signing and
notarising runbook is in [`docs/release/`](../../../docs/release/release-procedure.md).

## Testing

There is no test framework beyond cargo's, no `enable_testing()` and no CI
test target; the C++ follows a pattern of standalone assertion programs
built behind an option and run by hand (`libraries/tagcore/test/README.md`).
The Rust side has four kinds of check, from cheapest to dearest:

1. **Unit and integration tests, no hardware** (`cargo test`). Pure logic:
   parsing, framing, the ABI layout, text and JSON spelling, `ConfigShape`
   against the real default configurations, the models, the view-state
   builders, the engines driven with messages, the C-bridge wrappers on
   committed fixtures. These run in CI-less fashion today: run them before a
   change and after.
2. **Oracles against the Qt applications.** Tag Monitor reads qtmonitor's
   `test/` files in place (the command table, start checks, menus, and the
   710 configuration probe cases with goldens); Tag Calibrator and
   `calibsession` replay qtcalibrate's fixtures and compare logs and figures;
   `sensorviz-session` compares with the core's state dump. The Qt
   application is the reference; its test files change in the same commit
   as its behaviour, and the Rust tests then fail until the port matches.
   Regenerating goldens is a documented procedure in
   `applications/qtmon/test/README.md`.
3. **Byte-identical outputs.** The Rust `tag-dwnld` and the C++ one, on the
   same tag or the same import, must write identical databases (`cmp`);
   the `log-import` example exists for this. The screenshots of a page
   before and after a change are compared pixel for pixel.
4. **Hardware tests**, marked `#[ignore]` so `cargo test` skips them:
   `tagcore`'s `tests/hardware.rs` (attaches, read-only requests, one
   refused write, the UID check), `tagmonitor-core`'s `session_check`
   example (a whole tag cycle), Tag Programmer's `bench` example. They reset
   the tag; read the bench procedures first, and quit every other program
   that might hold the base.

```sh
cargo test -p tagcore --test hardware -- --ignored --test-threads=1   # a tag on a base
cargo run -p tagmonitor-core --example session_check                   # changes the tag; see its header
```

## Documentation checks

Every crate warns on undocumented items, private ones included, and the
rustdoc must build with warnings denied:

```sh
cargo clippy --all-targets -p <crate> 2>&1 | grep -c "missing documentation"   # expect 0
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items -p <crate>
```

For the Markdown, from the repository root:

```sh
docs/tools/docs.py check    # front matter, file names, relative links, index freshness
docs/tools/docs.py index    # regenerate docs/index.md after adding or retitling a document
git diff --check            # whitespace
```

The C++ is held to Doxygen coverage by diffing the warning log against the
project's own configuration (the recipe is in the root `AGENTS.md`).

## What CI does

`.github/workflows/release.yml`, on a version tag, builds the host package
on macOS, Windows and Linux with `-DBUILD_RUST_APPS=ON`: it installs stable
Rust, caches the workspace's dependency builds under the build tree's cargo
directory, builds everything with `cmake --build`, checks that each Tauri
executable embeds its page (`grep` for `/app.js` in the binary), packages,
and on Linux installs the package into clean Ubuntu and Debian containers
and checks that every application starts. The macOS DMG is then signed and
notarised locally. `.github/workflows/docs.yml` builds the user manual and
the developer portal, rustdoc included, and publishes them. Neither runs
`cargo test`.

## Where to read next

- The root [`README.md`](../../../README.md) for prerequisites per
  platform and the presets.
- [`docs/build/README.md`](../../../docs/build/README.md) for the build and
  reproducibility model, and the toolchain inventory.
- [`docs/release/release-procedure.md`](../../../docs/release/release-procedure.md)
  for cutting a release.
