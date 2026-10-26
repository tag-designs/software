---
type: design
status: current
summary: How Tag Programmer is built and shipped - the cargo workspace, what build.rs stages and why, the CMake target that wraps cargo, the per-platform install layout and signing, the CI build, the third-party notices, and the platform pitfalls each of those choices answers.
---

# Building and packaging Tag Programmer

Tag Programmer is built by cargo and shipped inside the host tools package
beside the Qt applications. The top-level CMake build runs cargo through a
custom target, and installs the binary where each platform's package expects
an application. Nothing outside the Rust toolchain is needed on macOS and
Windows: libusb and protoc are vendored by crates. Linux needs WebKitGTK 4.1,
at build time and on the user's machine. The build writes nothing into the
source tree.

## The workspace

| Crate | Path | Role |
| --- | --- | --- |
| `tagprogram` | `app/` | The Tauri shell, the only binary |
| `tagprogram-core` | `core/` | Model and engine; no UI toolkit, unit-testable |
| `tagprobe` | `../../libraries/tagprobe-rs` | probe-rs programming (path dependency) |
| `tagcore` | `../../libraries/tagcore-rs` | The monitor protocol over ST-LINK (path dependency) |

The workspace root is `host/applications/tagprogram-rs`, and its `Cargo.lock`
is committed. Release builds use `--locked`, so a build uses exactly the
reviewed dependency versions.

### Vendored native dependencies

- **libusb.** tagcore enables `rusb`'s `vendored` feature, which compiles
  libusb from source and links it statically. The packages carry no libusb
  library, and no system copy is used.
- **protoc.** tagcore's `build.rs` generates the protobuf types from `proto/`
  with prost. If `PROTOC` is not set, it points it at the binary from the
  `protoc-bin-vendored` crate. A developer machine with no protoc still
  builds.

## `app/build.rs`: staging the page

Tauri embeds the web page into the binary at compile time
(`generate_context!`), from the directory its configuration names as
`frontendDist`. The page is not one directory in the tree. It is `ui/` plus
the shared kit in `host/ui-kit/`, which `ui/index.html` loads by a relative
path (`../../../ui-kit/`), so the page also opens as a plain file in a
browser. `build.rs` therefore assembles a staging directory under cargo's
`OUT_DIR`:

```text
$OUT_DIR/tauri/
  dist/            ui/* with the kit path rewritten to ui-kit/, and
  dist/ui-kit/     host/ui-kit/*
  capabilities/    app/capabilities/*
  icons/           app/icons/*
  tauri.conf.json  Cargo.toml
```

It passes the staged `dist/` to `generate_context!` as an absolute path in
`TAURI_CONFIG`, then runs `tauri_build::build()` with the staging directory as
its working directory. Two facts force this layout:

- tauri-build writes `gen/schemas/` relative to its working directory. Run in
  `app/`, it wrote into the source tree. The CI Linux check builds with the
  source tree read-only, and failed.
- tauri-build reads `Cargo.toml` from its working directory, to check the
  `tauri` crate's features against the configuration. So the manifest is
  staged too.

`cargo:rerun-if-changed` lines cover every staged source, so editing the page
or the kit rebuilds the app.

## The CMake target

`host/applications/tagprogram-rs/CMakeLists.txt` is included from
`host/CMakeLists.txt` when `BUILD_RUST_APPS` is on. The option defaults to on
when cargo is found, and the release workflow turns it on explicitly. The
target `tagprogram`:

- runs `cargo build --release --locked -p tagprogram` every time and leaves
  the up-to-date decision to cargo;
- sets `CARGO_TARGET_DIR` to `<build>/host/applications/tagprogram-rs/cargo`,
  so a CMake build never shares cargo's output with a developer's
  `cargo build` in the source tree;
- passes `TAGPROGRAM_HOST_VERSION`, the host package version. The record's
  `host_version` column then reads `tagprogram-rs <package version>`, the
  number qtprogram writes. A plain `cargo build` uses the crate version;
- on macOS, passes `MACOSX_DEPLOYMENT_TARGET`, so the binary runs on the same
  macOS versions as the Qt apps.

### Install layout

| Platform | Installed as | Notes |
| --- | --- | --- |
| macOS | `tag_tools/tagprogram.app` | Bundle assembled by CMake: binary, `Info.plist` from `cmake/Info.plist.in`, `icon.icns`. Signed by `macos_codesign_bundle`, as the Qt bundles are. |
| Windows | beside the Qt executables | Self-contained; uses the WebView2 runtime that Windows 10 and 11 carry. |
| Linux | `tag_tools/bin/tagprogram` | Links the system WebKitGTK 4.1 (`libwebkit2gtk-4.1-0`). |

The bundle is assembled by CMake rather than by Tauri's bundler. That keeps
one signing and notarising path for every app in the DMG, and keeps
Tauri's bundler and its updater out of the release.

### macOS privacy strings

`Info.plist` declares `NSDocumentsFolderUsageDescription`, and the Desktop
and Downloads equivalents. The record lives in `~/Documents/Tag Programmer`
by default. For an app identity it has not seen before, macOS holds the first
`open()` there until the user answers a privacy prompt, and without a usage
string the prompt cannot say why. The engine also defers that open until the
window has drawn ([engine design](engine.md#deferring-work-to-the-next-pass)).

### Linux: no Documents directory

On a Linux account without XDG user directories, Tauri's `document_dir()`
fails. `main.rs` falls back to `~/Documents` (created when the record is), so
the app starts in a bare container. The first CI package test panicked here
before the fallback was added.

## The user guide

Help > User Guide opens the manual page `apps/tagprogram.html`. `main.rs`
looks for the built manual next to the executable: `docs/`, `host/docs/site/`
and `share/UltralightTags/docs/`, in up to six parent directories, which
covers the installed layouts and a build tree. A debug build also tries the
source tree's `host/docs/site`. Otherwise it opens the published manual on
the web. The capability file grants the opener `file://**` URLs for the local
manual.

## CI

`.github/workflows/release.yml` builds all three packages with
`-DBUILD_RUST_APPS=ON`. Its Rust steps:

- install stable Rust (`dtolnay/rust-toolchain`); the crates need 1.85 or
  newer, for edition 2024;
- cache the dependency builds in the CMake build tree's cargo directory
  (`Swatinem/rust-cache`);
- on Linux, install `libwebkit2gtk-4.1-dev`.

The package checks know about the app:

- `host/tools/linux-package-test.sh` installs the WebKitGTK runtime in clean
  Ubuntu and Debian containers, and checks that `tagprogram` starts and stays
  up;
- `host/tools/verify-macos-dmg.sh` applies its Qt TLS-plugin check only to
  bundles that contain QtCore;
- the [release procedure](../../../../docs/release/release-procedure.md)
  expects six app lines.

`host/tools/linux-build-check.sh` reproduces the Linux build in OrbStack, with
rustup and WebKitGTK installed and the source tree mounted read-only.

## Third-party notices

The binary statically contains about 530 crates. Most of their licenses
require their notice to travel with a binary, so
`LICENSES/host/tagprogram-THIRD-PARTY-NOTICES.txt` lists every crate reached
by normal (shipped) dependencies, with its license and license files. It is
generated by `LICENSES/tools/generate_rust_notices.py` from `cargo metadata
--locked`. **Regenerate it whenever `Cargo.lock` changes**:

```sh
LICENSES/tools/generate_rust_notices.py host/applications/tagprogram-rs \
    tagprogram > LICENSES/host/tagprogram-THIRD-PARTY-NOTICES.txt
```

libusb, compiled in from source, is LGPL-2.1, and has its own notice in
`LICENSES/host/third-party/libusb.txt`.

## Size

The macOS app is about 21 MB. qtprogram's bundle is about 51 MB, and it also
needs STM32CubeProgrammer (about 1.7 GB) installed. Most of Tag Programmer's
size is probe-rs's target database and flash algorithms, and the webview
glue.
