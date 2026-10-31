---
type: design
status: current
summary: Chapter 12 of the Host Software Guide - every external module the host software depends on (the Rust crates, the vendored JavaScript libraries, the C++ libraries and Qt, and the tools), what each is for, why it was chosen or what it replaced, where its version is pinned, and the rules for adding or updating one.
---

# 12. External dependencies

This chapter is the inventory. It assumes chapter 2. Versions are those
resolved on 2026-10-31; the pins are in `host/Cargo.lock` (Rust),
`vcpkg.json` and `vcpkg-configuration.json` (C++), the vendored files'
headers (JavaScript), and `LICENSES/host/README.md` (the licence table the
packages carry). Where a figure here and a lock file disagree, the lock file
is right.

## Rust crates

A Rust program statically contains every crate it depends on, directly or
through another crate: Tag Programmer carries about 530 crates, Tag View
about 440. The tables list only the **direct** dependencies, which are
what a reader meets in the code; the generated notices files list every
crate with its licence.

### Talking to hardware

| Crate | Version | Used by | For | Notes |
| --- | --- | --- | --- | --- |
| `rusb` | 0.9.4 | tagcore | Safe bindings to libusb: enumerating ST-LINKs, bulk transfers | The `vendored` feature compiles libusb 1.0.29 from source and links it statically, so no system libusb is needed (Linux needs `libudev`). libusb is LGPL-2.1 and has its own notice |
| `probe-rs` | 0.32.0 | tagprobe | Flash programming and debug-port access for Arm targets: the ST-LINK driver, the target database with the STM32L432 and STM32U375 flash algorithms, sessions under reset | Chosen after an [evaluation](../../../docs/investigations/2026-10-probe-rs-evaluation.md) against ST's CLI and libstlink; replaced STM32CubeProgrammer. A large dependency (tokio, tracing and a target database come with it) |
| `nusb` | 0.2.9 | tagprobe | A pure-Rust USB library, used for the DFU path | probe-rs's own USB layer; `dfu-nusb` builds on it |
| `dfu-core`, `dfu-nusb` | 0.11.1, 0.2.0 | tagprobe | The USB DFU protocol and ST's DfuSe dialect, for programming bases through their ROM bootloader | Replaced `dfu-util`. `dfu-nusb` wants a tokio runtime for its async path; the synchronous path is used |
| `tokio` | 1.54.1 | tagprobe | An async runtime, only because `dfu-nusb` requires one to be named | Already in the graph through probe-rs; nothing in the host code is async |
| `object` | 0.39.1 | tagprobe | Reading ELF files: the load segments `base-flash` flattens | Tag Programmer has its own minimal ELF reader (`model/elf.rs`) for symbols and segments, written before `object` came in with the DFU work |
| `tracing` | 0.1.44 | tagprobe | probe-rs logs through `tracing`; the crate bridges it to `log` | |

### Protocol and data

| Crate | Version | Used by | For | Notes |
| --- | --- | --- | --- | --- |
| `prost`, `prost-build` | 0.14.4 | tagcore, taglogwriter, tagmonitor-core | Protocol Buffers: the generated message types and their encoding | The Rust counterpart of the C++ protobuf library; chosen for its code generation at build time from the shared `.proto` files |
| `prost-reflect`, `prost-reflect-build` | 0.16.5, 0.16.1 | tagcore, tag-tools, tagmonitor-core | Reflection over the generated messages: the text format and JSON spelled as the C++ prints them, and `ConfigShape`'s walk of `Config` by field name | The `serde` feature lets `serde_json` read and write messages |
| `protoc-bin-vendored` | 3.3.0 | tagcore (build) | A `protoc` binary for every platform, so a machine without protoc still builds | `PROTOC` in the environment overrides it |
| `serde`, `serde_json` | 1.0.229, 1.0.154 | most crates | JSON: the view states, settings files, the catalog, sensorViz's descriptions across the C interface, the capture file | `serde` with `derive` where a crate defines its own JSON types |
| `base64` | 0.22 | sensorviz-core | The PDF bytes the page sends to be saved | |
| `sha2` | 0.10.9 | tagprogram-core | SHA-256 of firmware images, checked against release manifests | |
| `regex` | 1.13.1 | tagprogram-core | Parsing release listings and manifests | |
| `tar`, `flate2` | 0.4.46, 1.1.10 | tagprogram-core | Extracting downloaded release archives | |
| `reqwest` | 0.12.28 | tagprogram-core | HTTP: listing and downloading GitHub releases, blocking, with `rustls-tls` so no system TLS library is needed | |
| `chrono` | 0.4.45 | tag-tools, tagprogram-core | Local time in log lines and record rows | |

### Programs and user interface

| Crate | Version | Used by | For | Notes |
| --- | --- | --- | --- | --- |
| `clap` | 4.6.7 | tag-tools | Command-line parsing with `derive`; `--help` from doc comments | Replaced cxxopts; option errors exit 2 rather than 1 |
| `env_logger`, `log` | 0.11.11, 0.4.34 | all | The `log` facade every crate logs through, and the stderr logger the tools install in `log.c`'s format | The applications install their own `log::Log` that feeds the log pane |
| `ctrlc` | 3.5.2 | tag-tools | Exit 1 on Ctrl-C, as the C++ tools' SIGINT handler did | |
| `tauri`, `tauri-build` | 2.12.5, 2.7.4 | the four `app` crates | The native window with the platform's webview, `invoke` from the page, events to it | Underneath: `wry` 0.57 (the webview) and `tao` 0.37 (the window). Tauri's bundler and updater are not used |
| `tauri-plugin-dialog` | 2.8.3 | the `app` crates | Native file open and save dialogs | |
| `tauri-plugin-opener` | 2.7.2 | the `app` crates | Opening the user guide's URL or file, and showing a file in its folder | |
| `objc2-foundation` | 0.3.2 | the `app` crates (macOS) | `NSProcessInfo` to opt out of App Nap, which throttled the status poll | Already in the graph through Tauri |
| `tempfile` | 3 (dev) | tagprogram-core | Scratch directories in tests | |

### Platform webviews (not crates)

| Platform | Webview | Needed where |
| --- | --- | --- |
| macOS | WebKit, part of the system | nothing to install |
| Windows | WebView2, part of Windows 10 and 11 | nothing to install |
| Linux | WebKitGTK 4.1 (`libwebkit2gtk-4.1-0`) | at build time (`-dev`) and on the user's machine; the package test installs it in clean containers |

## JavaScript, vendored in the pages

No npm and no build step: each library is a published build committed with
a header naming its version, loaded as a classic script.

| Library | Version | Where | For | Why this one |
| --- | --- | --- | --- | --- |
| Preact + hooks | 10.29.8 | `ui-kit/preact-htm.js` | Rendering each page from its view state, diffing against the previous render | React-compatible and 4 KB; a page redraws from one state, which is what a virtual DOM is for ([decision 0038](../../../docs/decisions/0038-host-ui-kit-preact.md)); Lit, Svelte, Solid, React, Vue were considered |
| htm | 3.1.1 | `ui-kit/preact-htm.js` | JSX-like templates as tagged strings, so no compiler | |
| uPlot | 1.6.32 | `applications/sensorviz-rs/ui/` | Tag View's plot: stacked axes, time axes, fast redraws | Chosen by a spike: draws every sensorViz feature, under a millisecond per redraw; plotters and a hand-written canvas were the alternatives |
| jsPDF | 2.5.2 | `applications/sensorviz-rs/ui/` | PDF export at 300 dpi | The browser's print and plotters were rejected (see the README) |
| three.js | r159 | `applications/tagcalibrate-rs/ui/` | The sample sphere and the tag model | The last classic (non-module) build |

Updating one is a documented manual step (the kit README for Preact and
htm); each has a notice in `LICENSES/host/third-party/`.

## C++ libraries

Linked into the C++ programs, and into `libtagcore_c` for the Rust ones.
Installed through vcpkg on Windows (static, `x64-windows-static-md`) and
macOS, from the system on Linux, at the baseline in
`vcpkg-configuration.json`.

| Library | Version | For | Notes |
| --- | --- | --- | --- |
| Protocol Buffers (with Abseil, utf8_range) | 6.33.4 | The message types, text format and JSON in the C++ host, and inside `tagcore_c` | The firmware uses nanopb 0.4.9.1 instead ([decision 0011](../../../docs/decisions/0011-build-pin-nanopb-0-4-9-1.md)) |
| SQLite | 3.53.1 | The log writer, `sensordata`, `dataprocessing` | Native API, never QtSql, in shared code |
| libusb | 1.0.29 | The C++ `LinkAdapt` | The same library the Rust side vendors |
| Qt | 6.10.3 | The Qt applications: Core, Gui, Widgets, Concurrent, PrintSupport, Svg, SvgWidgets, Qml, Quick, QuickWidgets, Quick 3D | Pinned everywhere with a macOS 13 floor ([decision 0026](../../../docs/decisions/0026-host-qt-6-10-3-and-macos-13-floor.md)); Quick 3D is GPL-only, which makes `qtcalibrate` GPL as distributed |
| QCustomPlot | 2.1.1 | `sensorviz` and `btviz` plots | GPL-3.0; vendored in `libraries/qcustomplot` |
| cxxopts | 3.0.0 | The C++ tools' options | |
| rxi `log.c` | 0.1.0 | The C++ host logging, whose line format the Rust tools copy | |
| stlink `commands.h` | | The ST-LINK command codes, in both the C++ and the Rust link layers | From the stlink-org project |
| Freescale magnetic calibration | | The magcal solver inside `sensoranalysis/core` | |
| Kiss FFT | 1.3.0 | `btviz` | |
| grpcxx | fetched | `tag-server` only (`BUILD_TAG_SERVER=ON`) | Not in the release build |

## Tools

| Tool | Needed for | Version |
| --- | --- | --- |
| Rust toolchain (rustup: `cargo`, `rustc`, `clippy`, `rustdoc`) | Everything in the workspace | 1.85 or newer (edition 2024); CI uses current stable |
| CMake | The whole build, including running cargo | 3.20 or newer; `tag-server` needs 3.28 |
| A C++20 compiler | The C++ libraries and `libtagcore_c` | MSVC on Windows, clang on macOS, gcc on Linux |
| vcpkg | The C++ libraries on Windows and macOS | `VCPKG_ROOT`; the manifest is `vcpkg.json` |
| Qt | The Qt applications (`BUILD_QT_APPS`) | 6.10.3 |
| `protoc` | The C++ protobuf code | From vcpkg or the system; the Rust side needs none |
| Python 3 with MkDocs Material | The user manual and the developer portal | `host/docs/requirements.txt` |
| Doxygen | The portal's C++ API reference (optional) | |
| headless Chrome | `ui-kit/screenshot.sh`, the manual's pictures of the Rust applications | |
| STM32CubeProgrammer (`STM32_Programmer_CLI`) | **Only as a fallback**: qtprogram, and the CMake programming targets when cargo is missing and `tag-flash` is not on `PATH` | |

The full inventory across the project's repositories, with install paths,
is [`docs/build/toolchain-inventory.md`](../../../docs/build/toolchain-inventory.md).

## Rules

- **Pin everything.** Rust versions are pinned by `host/Cargo.lock`, which
  release builds use with `--locked`; a change to a dependency edits the
  lock once, for every crate.
- **Regenerate the notices.** Whenever the lock changes, run
  `LICENSES/tools/generate_rust_notices.py` for all four applications; the
  files are installed into the packages, and most crate licences require
  their notice to travel with a binary.
- **Prefer what is already in the graph.** probe-rs brings tokio and
  tracing; Tauri brings `objc2`. A new direct dependency on something
  already there costs nothing; a new tree costs build time, binary size and
  a licence review.
- **Vendor what the page needs; never a build step.** A page must open from
  `file://` against its mock. A library that needs npm to use is the wrong
  library.
- **No Rust crate depends on Qt**, and `tagcore`, `tagcore-c` and
  `sensordata` depend on no Qt either.
