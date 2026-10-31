---
type: design
status: current
summary: Chapter 2 of the Host Software Guide - the layout of the host tree and the places outside it the host depends on, what every directory, crate, executable and program is called, the shape of a crate and of an application directory, how CMake and cargo divide the build, and where to look things up.
---

# 2. The source tree

This chapter is the map. It assumes chapter 1's vocabulary and nothing
else. Read it with a file browser open; it is meant to make the directory
listing make sense.

## The repository, from the host's point of view

```text
software/
├── proto/            tag.proto, tagdata.proto: the protobuf schema host and firmware share (chapter 4)
├── include/          C headers both sides compile: monitor.h, *_log_format.h, loader_service.h (chapter 4)
├── embedded/         the firmware; the host reads two things from it:
│   └── proto-c/<variant>-proto-c/default-config.json   which Config fields each tag has (chapter 4)
├── host/             THIS TREE
├── cmake/            CMake helpers; HostRust.cmake and RustFlashTools.cmake run cargo (chapter 13)
├── docs/             cross-cutting developer docs: architecture, shared contracts, bench, release, decisions
├── LICENSES/host/    the notices the host packages carry, including the generated Rust ones
└── .github/workflows/release.yml   builds and packages the host tools on macOS, Windows and Linux
```

## The host tree

```text
host/
├── Cargo.toml, Cargo.lock      the one Cargo workspace for every Rust crate below
├── CMakeLists.txt              wires the C++ libraries, the Qt apps, the Rust apps and packaging together
├── README.md, AGENTS.md        the map (tables of directories, crates and names) and the working rules
├── libraries/
│   ├── tagcore/                C++: the original tag interface, ConfigShape, the SQLite and text log writers,
│   │                                the SWD capture and recovery library (recovery/)
│   ├── tagcore-rs/             Rust: the tag interface, ported from tagcore's Tag classes      (chapter 6)
│   ├── tagprobe-rs/            Rust: programming through probe-rs; bases over DFU               (chapter 7)
│   ├── tagcore-c/              C++: the C interface (tlw_*, tcs_*, svz_*) Rust calls            (chapter 8)
│   ├── tagcore-c-sys/          Rust: links tagcore_c and declares its functions                 (chapter 8)
│   ├── taglogwriter-rs/        Rust: the SQLite writer, through tlw_*                           (chapter 8)
│   ├── calibsession-rs/        Rust: the calibration session, through tcs_*                     (chapter 8)
│   ├── sensorviz-session-rs/   Rust: sensorViz's core, through svz_*                            (chapter 8)
│   ├── sensoranalysis/         C++: compass calibration and orientation; its core/ is Qt-free
│   ├── sensordata/             C++: decimating SQLite log reader (sqlite3 only)
│   ├── sensorui/               C++/Qt: shared widgets for the Qt apps
│   └── qcustomplot/            vendored plotting for the Qt viewers
├── commandline/                C++ tools: tag-*.cc, dwnld.cc, dataprocessing.cc, the recovery tools
├── commandline-rs/             Rust tools: src/bin/<tool>.rs, src/lib.rs (what they share)     (chapter 9)
├── applications/
│   ├── qtmon/ qtprogram/ qtcalibrate/ sensorviz/ btdataviz/      the Qt applications
│   ├── tagprogram-rs/          Tag Programmer   ┐  each: core/ (engine crate), app/ (Tauri shell crate),
│   ├── tagmonitor-rs/          Tag Monitor      │        ui/ (the page), design/ (architecture, contract),
│   ├── tagcalibrate-rs/        Tag Calibrator   │        CMakeLists.txt, cmake/Info.plist.in   (chapters 10, 11)
│   └── sensorviz-rs/           Tag View         ┘
├── ui-kit/                     the web front ends' shared CSS, Preact components and plumbing   (chapter 10)
├── common/                     Qt helper headers (host_common)
├── tagserver/, python/tagclient/   the C++ tag interface over gRPC, and its Python client (tests only)
├── python/tagsim/              the IMUTag log simulator (test data)
├── spikes/probe-rs-spike/      the experiment behind tagprobe; not built with the host
├── design/                     host-wide design docs: this guide, Linux packaging, proposals
├── docs/                       the user manual (MkDocs) and its screenshot fixtures
├── linux/                      the udev rule
└── tools/                      release scripts: signing, Linux build and package checks
```

Two directory conventions:

- **`-rs` means "the Rust counterpart of the C++ directory beside it".**
  `tagcore-rs` ports `tagcore`'s tag classes; `tagmonitor-rs` ports
  `qtmon`; `sensorviz-rs` ports `sensorviz`'s window. Where there is no C++
  counterpart the suffix still marks Rust (`tagprobe-rs`), except for
  `tagcore-c-sys`, named after the Rust convention for a crate that links a
  C library.
- **Every directory has a `README.md` and, where there is a design, a
  `design/`.** The README says what is there and how to use it; the design
  documents say how it works. `docs/index.md` lists every one, generated
  from their front matter.

## Names

A program has up to four names. Keep this table at hand until they are
familiar; the host README carries the same table.

| Directory | Crate (Cargo) | Executable | Product name | Ported from |
| --- | --- | --- | --- | --- |
| `libraries/tagcore-rs` | `tagcore` | | | `libraries/tagcore` (`tagclass`, `tagmonitor`, `linkadapt`, `configshape`) |
| `libraries/tagprobe-rs` | `tagprobe` | | | nothing; replaces `STM32_Programmer_CLI` |
| `libraries/tagcore-c-sys` | `tagcore-c-sys` | | | |
| `libraries/taglogwriter-rs` | `taglogwriter` | | | wraps `tagcore/sqlitelog` |
| `libraries/calibsession-rs` | `calibsession` | | | wraps `sensoranalysis/core/calibration_session` |
| `libraries/sensorviz-session-rs` | `sensorviz-session` | | | wraps `applications/sensorviz/core` |
| `commandline-rs` | `tag-tools` | `tag-info`, `tag-start`, ..., `tag-flash`, `base-flash` | | `commandline/*.cc` |
| `applications/tagprogram-rs` | `tagprogram-core`, `tagprogram` | `tagprogram` | Tag Programmer | `applications/qtprogram` (`qtprogram`) |
| `applications/tagmonitor-rs` | `tagmonitor-core`, `tagmonitor` | `tagmonitor` | Tag Monitor | `applications/qtmon` (`qtmonitor`) |
| `applications/tagcalibrate-rs` | `tagcalibrate-core`, `tagcalibrate` | `tagcalibrate` | Tag Calibrator | `applications/qtcalibrate` (`qtcalibrate`) |
| `applications/sensorviz-rs` | `sensorviz-core`, `tagview` | `tagview` | Tag View | `applications/sensorviz` (`sensorviz`, product name SensorViz) |

Rust crate names use hyphens in `Cargo.toml` and underscores in code and
rustdoc: the crate `tagcore-c-sys` is `tagcore_c_sys` in a `use` line, and
`tag-tools` is `tag_tools`.

## The shape of a Rust crate

Every library crate here has the same layout, so once you have read one you
can find your way in the others:

```text
libraries/tagcore-rs/
├── Cargo.toml          name, dependencies, features; a comment saying what the crate is; the documentation lints
├── README.md           what it is, the file map, how to build and test it, how it differs from what it ports
├── design/architecture.md   how it works and why (only where there is enough to say)
├── build.rs            a build script, if the crate generates code (protobuf) or links a C library
├── src/lib.rs          the crate root: a `//!` doc comment that is the crate's front page, then `pub mod` lines
├── src/<module>.rs or src/<module>/mod.rs   one concern per file; a type's methods may be spread over a folder
├── tests/              integration tests (black-box, through the public API); hardware tests are `#[ignore]`
└── examples/           small programs that show the API; some are bench tools
```

Three habits of this code base's Rust worth knowing before you open a file:

- **The first comment is the summary.** Every file starts with a `//!`
  block saying what it holds and, for a port, which C++ file it follows.
  Every item has a `///` comment; the crates warn when one is missing
  (`missing_docs`, `clippy::missing_docs_in_private_items`). Reading the
  doc comments top to bottom is a legitimate way to read a module.
- **One type, one folder.** `TagMonitor` is one `struct`, but its methods
  are in `monitor/mod.rs`, `monitor/l4.rs`, `monitor/u3.rs`, `monitor/rpc.rs`
  and so on, each an `impl TagMonitor` block for one concern. Rust allows a
  type's `impl` blocks to be spread across files in the same crate; this code
  uses that to keep each file about one thing.
- **Tests sit with the code.** Unit tests are a `#[cfg(test)] mod tests` at
  the end of the file they test. Integration tests in `tests/` are the
  oracles against the C++ (chapter 13).

## The shape of a Rust application

```text
applications/tagmonitor-rs/
├── Cargo.toml                 a comment only: the two crates below are workspace members
├── README.md                  what is ported, status, how to build, run and check it
├── design/
│   ├── architecture.md        layers, threads, the source map, how a command travels, where to change what
│   ├── ui-engine-contract.md  the view state, prompts, commands and queries between engine and page
│   └── build-and-packaging.md how it is built by cargo and CMake and installed per platform
├── core/                      crate tagmonitor-core: the engine (no UI toolkit)
│   ├── src/model/             pure rules, ported module for module from the Qt app's core (Tag Monitor only)
│   ├── src/engine/            the threads: engine loop, tag thread, the state, the view-state builder
│   ├── tests/                 the oracles against the Qt application
│   └── examples/              headless runs: a whole tag cycle on hardware, a form printer
├── app/                       crate tagmonitor: the Tauri shell
│   ├── src/main.rs            starts the engine; two Tauri commands in, two events out
│   ├── build.rs               stages ui/ and the kit into Tauri's distribution directory
│   ├── tauri.conf.json, capabilities/default.json, icons/
├── ui/                        the page: index.html, app.js (Preact), app.css, mock-engine.js, screenshots.sh
├── CMakeLists.txt             the CMake target (host_rust_app) and the per-platform install
└── cmake/Info.plist.in        the macOS bundle's property list
```

Chapter 10 explains why it is split this way; chapter 11 walks each
application.

## Two build systems, one build

CMake builds the C++ and packages everything; cargo builds the Rust. They
meet in two places:

- **CMake runs cargo.** `cmake/HostRust.cmake` defines `host_rust_app()`,
  which each Rust application's `CMakeLists.txt` calls to run
  `cargo build --release --locked -p <crate>` in the host workspace, with one
  shared target directory under the build tree (`<build>/host/cargo`), and
  copy the executable beside the C++ programs. `cmake/RustFlashTools.cmake`
  does the same for `tag-flash` and `base-flash`, which the embedded
  programming targets also use. `BUILD_RUST_APPS` (on where cargo is
  installed) turns all of this on.
- **cargo links a CMake product.** The crates that reach the C++ libraries
  link `libtagcore_c`, which only CMake can build (it needs protobuf, SQLite
  and the rest of the C++ toolchain). `tagcore-c-sys`'s build script finds it
  through the `TAGCORE_C_LIB_DIR` environment variable, which the CMake
  targets set and a developer sets by hand (chapter 13).

Everything else is independent: `cargo build -p tagcore` or
`cargo test -p tagprogram-core` needs no CMake, no Qt and no C++ compiler
beyond what the vendored libusb build uses.

## Where to look things up

| Question | Look in |
| --- | --- |
| What does this function do, what does it return on failure? | Its doc comment, or the developer portal: rustdoc under `rust/`, Doxygen under `api/` (`cmake --build <build> --target developer_docs`) |
| How does this crate or app work? | `README.md`, then `design/architecture.md` beside it |
| Why was it built this way? | `docs/decisions/` (0035 to 0041 are the Rust port), then the `design/proposals/` and `design/investigations/` of the owner |
| What do host and firmware agree on? | `docs/shared/` and chapter 4 |
| How do I do X on real hardware without wrecking evidence? | `docs/bench/`, and the rules in the root `AGENTS.md` |
| How is it packaged and released? | each app's `design/build-and-packaging.md`, `host/design/linux-package.md`, `docs/release/` |
| What is still open? | the `TODO.md` in the owning directory |
| How do users see it? | `host/docs/src/apps/*.md`, `cli/*.md` |

The index of every developer document is `docs/index.md`, generated by
`docs/tools/docs.py index` from each document's front matter; `docs.py check`
validates front matter and links. Never keep a list of documents by hand.
