---
type: readme
status: current
summary: Map of the host tree - the C++ libraries and Qt applications, the Rust crates and Tauri applications that are replacing them, how the two are joined, the names each program goes by, dependency direction, the CMake and cargo entry points, packaging notes and maintenance rules.
---

# Host Software

This directory holds everything that runs on a desktop computer to work with
Ultralight Tags: libraries that talk to a tag through its base, command-line
tools, graphical applications, and the packaged user manual.

**New to the project? Start with the
[Host Software Guide](design/guide/README.md)**, which explains the system,
the communication and storage models, and each component's architecture and
code structure, with the reading order a newcomer needs. This README is the
map; the guide is the book.

The host code exists in two implementations, and both ship today:

- **C++**: the `tagcore` library (the original tag interface, the SQLite
  writer, the SWD recovery library), the sensor libraries, the Qt
  applications (`qtmonitor`, `qtprogram`, `qtcalibrate`, `sensorviz`,
  `btviz`) and most command-line tools.
- **Rust**: ports made in October 2026. A Rust tag interface (`tagcore-rs`),
  programming through probe-rs (`tagprobe-rs`), the command-line tools that
  need only the tag interface (`commandline-rs`), and four Tauri applications
  (Tag Programmer, Tag Monitor, Tag Calibrator, Tag View). Where the C++ holds
  logic that must not be duplicated (the SQLite writer, the calibration
  session, sensorViz's core) the Rust reaches it through one C interface,
  `tagcore_c`, rather than porting it. Rust may depend on the C++; a C++-free
  stack is not a goal ([decisions 0035 to 0041](../docs/decisions/README.md)).

## Layout

| Directory | Role |
| --- | --- |
| `libraries/` | Reusable code, C++ and Rust: the tag interface, programming, log writing and reading, sensor maths, shared widgets, and the C interface between the two languages. See [libraries/README.md](libraries/README.md). |
| `applications/` | The applications, Qt (`qtmon`, `qtprogram`, `qtcalibrate`, `sensorviz`, `btdataviz`) and Rust/Tauri (`*-rs`). See [applications/README.md](applications/README.md). |
| `commandline/` | The C++ command-line tools (`tag-*`, `dataprocessing`), on `libraries/tagcore`. See [commandline/README.md](commandline/README.md). |
| `commandline-rs/` | The Rust command-line tools: ports of the C++ `tag-*` tools, plus `tag-flash` and `base-flash`, which program tags and bases without STM32CubeProgrammer. See [commandline-rs/README.md](commandline-rs/README.md). |
| `ui-kit/` | The web front ends' shared look and plumbing: CSS, Preact components, the engine connection, menus and dialogs. See [ui-kit/README.md](ui-kit/README.md). |
| `common/` | Small Qt helper headers shared by the Qt applications (`host_common`). |
| `tagserver/` | `tag-server`, the C++ tag interface over gRPC for test programs; `BUILD_TAG_SERVER=ON` only. |
| `python/` | `tagclient`, the Python client for `tag-server`; `tagsim`, the IMUTag log simulator (testing infrastructure). |
| `spikes/` | Experiments kept for their results, not built with the host (`probe-rs-spike`). |
| `design/` | Host-wide design documents: the [guide](design/guide/README.md), [Linux packaging](design/linux-package.md), proposals. |
| `docs/` | The MkDocs user manual and the fixtures its screenshots come from; built by the `docs` target, installed with `BUILD_HOST_DOCS=ON`. See [docs/README.md](docs/README.md). |
| `linux/` | The udev rule that lets an ordinary user open a base. |
| `tools/` | Release and packaging scripts (signing, Linux build and package checks). |
| `Cargo.toml`, `Cargo.lock` | The one Cargo workspace for every Rust crate below, and its lock file (see [Rust crates](#rust-crates)). |

## Rust crates

All Rust code under `host/` is one Cargo workspace, rooted at
`host/Cargo.toml` ([decision 0041](../docs/decisions/0041-host-one-cargo-workspace.md)).
From this directory `cargo build`, `cargo test`, `cargo doc` and
`cargo clippy` cover every crate, rust-analyzer sees one project, and one
`Cargo.lock` pins every dependency for every build, the CMake targets
included. Directory names end in `-rs` where a C++ directory of the same name
exists; the crate and program names do not, so this table is the one to
keep open:

| Directory | Crate | Builds | Role |
| --- | --- | --- | --- |
| `libraries/tagcore-rs` | `tagcore` | library | The tag interface over an ST-LINK base: `Tag`, `TagMonitor`, `LinkAdapt`, the protobuf types (generated from `proto/`), `ConfigShape`. A port of the C++ `tagcore` tag classes. |
| `libraries/tagprobe-rs` | `tagprobe` | library | Programming tags through probe-rs, with a field-safe driver for old bases; programming bases over USB DFU. |
| `libraries/tagcore-c-sys` | `tagcore-c-sys` | library | Links the C++ `tagcore_c` library and declares its C functions (`tlw_*`, `tcs_*`, `svz_*`). The one crate that links it. |
| `libraries/taglogwriter-rs` | `taglogwriter` | library | Writes tag SQLite logs through the C++ writer (`tlw_*`). |
| `libraries/calibsession-rs` | `calibsession` | library | The magnetometer and accelerometer calibration session and compass angles, through the C++ core (`tcs_*`). |
| `libraries/sensorviz-session-rs` | `sensorviz-session` | library | Opens and views tag logs through sensorViz's C++ core (`svz_*`). |
| `commandline-rs` | `tag-tools` | 16 binaries | `tag-info`, `tag-start`, `tag-stop`, `tag-reset`, `tag-test`, `tag-dwnld`, `tag-flash`, `base-flash` and the bench tools; `src/lib.rs` is what they share. |
| `applications/tagprogram-rs/core` | `tagprogram-core` | library | Tag Programmer's model and engine. |
| `applications/tagprogram-rs/app` | `tagprogram` | **tagprogram** | Tag Programmer's Tauri shell: the program. |
| `applications/tagmonitor-rs/core` | `tagmonitor-core` | library | Tag Monitor's model and engine. |
| `applications/tagmonitor-rs/app` | `tagmonitor` | **tagmonitor** | Tag Monitor's Tauri shell. |
| `applications/tagcalibrate-rs/core` | `tagcalibrate-core` | library | Tag Calibrator's engine. |
| `applications/tagcalibrate-rs/app` | `tagcalibrate` | **tagcalibrate** | Tag Calibrator's Tauri shell. |
| `applications/sensorviz-rs/core` | `sensorviz-core` | library | Tag View's engine (sensorViz in Rust). |
| `applications/sensorviz-rs/app` | `tagview` | **tagview** | Tag View's Tauri shell. |

Excluded from the workspace, built on their own: `spikes/probe-rs-spike` and
`applications/sensorviz-rs/tools/print-spike`.

```sh
cd host
cargo build -p tagcore                 # one crate
cargo test -p tagcore -p tag-tools -p tagprobe -p tagprogram-core -p tagmonitor-core
cargo doc --no-deps --document-private-items -p tagcore   # into host/target/doc
```

The crates that link `tagcore_c` (`tagcore-c-sys` and the three wrappers,
and so `tagcalibrate`, `tagview`, `tag-dwnld` and Tag Monitor's download)
need that library built by CMake first and `TAGCORE_C_LIB_DIR` pointing at it;
[libraries/tagcore-c/README.md](libraries/tagcore-c/README.md) says how, and
the guide's [building chapter](design/guide/13-building-and-testing.md) lists
every environment variable and feature. A plain `cargo build --workspace`
without it fails in `tagcore-c-sys`'s build script with a message saying so;
build the crates you need with `-p`, or set the variable.

## Dependency direction

Dependencies flow from applications toward libraries, and from Rust toward
C++, never the other way:

```text
C++
  applications/{qtmon,qtprogram,qtcalibrate,sensorviz}  -> libraries/*, common/
  commandline                                           -> libraries/tagcore (dataprocessing: sensoranalysis_core)
  tagserver                                             -> libraries/tagcore, grpcxx
  libraries/sensorui                                    -> libraries/sensoranalysis
  libraries/tagcore-c                                   -> libraries/tagcore (sqlitelog), sensoranalysis_core, applications/sensorviz/core
  libraries/tagcore                                     -> proto/, include/

Rust
  applications/*-rs/app  -> that app's core, tauri
  tagprogram-core        -> tagcore, tagprobe
  tagmonitor-core        -> tagcore, taglogwriter (feature sqlitelog)
  tagcalibrate-core      -> tagcore, calibsession
  sensorviz-core         -> sensorviz-session, calibsession
  commandline-rs         -> tagcore, tagprobe, tagprogram-core (the erase plan), taglogwriter (feature sqlitelog)
  tagprobe               -> tagcore (its LinkAdapt), probe-rs
  taglogwriter, calibsession, sensorviz-session -> tagcore-c-sys -> libtagcore_c (C++)
  tagcore                -> proto/ (generated at build time), include/monitor.h (mirrored by hand)
```

`libraries/tagcore` and `libraries/sensordata` stay Qt-free, so the
command-line tools and `tagcore_c` need no Qt. Qt-only helpers belong in
`common/` or `libraries/sensorui`. No Rust crate uses Qt.

## Programs and their names

A program has up to three names: its source directory, its executable, and
the name the user manual gives it. Both implementations of each application
ship until the Qt one is retired.

| Product name | Executable | Source | Implementation | Does |
| --- | --- | --- | --- | --- |
| Tag Monitor | `qtmonitor` | `applications/qtmon` | C++/Qt | Attach, configure, start, stop, self-test, erase and download a tag |
| Tag Monitor | `tagmonitor` | `applications/tagmonitor-rs` | Rust/Tauri | The same, on the Rust tag interface |
| Tag Programmer | `qtprogram` | `applications/qtprogram` | C++/Qt | Program and test a batch of tags (through STM32CubeProgrammer) |
| Tag Programmer | `tagprogram` | `applications/tagprogram-rs` | Rust/Tauri | The same, through probe-rs |
| Tag Calibrator | `qtcalibrate` | `applications/qtcalibrate` | C++/Qt | Calibrate a tag's magnetometer and accelerometer, show its compass |
| Tag Calibrator | `tagcalibrate` | `applications/tagcalibrate-rs` | Rust/Tauri | The same, on the same C++ calibration session |
| SensorViz | `sensorviz` | `applications/sensorviz` | C++/Qt | View downloaded SQLite logs |
| Tag View | `tagview` | `applications/sensorviz-rs` | Rust/Tauri | The same, on the same C++ core |
| Bit Tag Visualizer | `btviz` | `applications/btdataviz` | C++/Qt | Legacy BitTag viewer (deprecated) |

The command-line tools keep one name in both languages: `tag-info` is
`commandline/tag-info.cc` and `commandline-rs/src/bin/tag-info.rs`, with the
same options, output and exit status. Only the C++ tools are installed, except
`tag-flash` and `base-flash`, which exist only in Rust and ship with the host
tools.

## CMake entry points

`host/CMakeLists.txt` wires the layout together:

- `libraries/tagcore`, `libraries/sensoranalysis`, `libraries/sensordata`,
  `applications/sensorviz/core` and `commandline` are added whenever host
  tools are built (`BUILD_HOST`).
- Qt support libraries and applications are added with `BUILD_QT_APPS=ON`.
- `libraries/tagcore-c` is added with `BUILD_TAGCORE_C=ON`, and always with
  `BUILD_RUST_APPS`, which needs it.
- The Rust applications and `commandline-rs` are added with
  `BUILD_RUST_APPS` (on by default where `cargo` is installed). Each is a
  custom target that runs `cargo build --release --locked` in the host
  workspace through `host_rust_app()` in
  [`cmake/HostRust.cmake`](../cmake/HostRust.cmake); every cargo run in a
  build tree shares one target directory, `<build>/host/cargo`, and the
  executables are copied to `<build>/<Config>/bin` beside the C++ programs.
  `tag-flash` and `base-flash` are built by `flash_tools`
  ([`cmake/RustFlashTools.cmake`](../cmake/RustFlashTools.cmake)), which the
  embedded `-download`, `-erase` and `-dfu` targets also depend on.
- `tagserver` is added with `BUILD_TAG_SERVER=ON` (default `OFF`).
- A small `host_common` interface target exposes `common/` headers to Qt apps.
- The `docs` target builds the MkDocs user guide. `BUILD_HOST_DOCS` adds it to
  the default build and the package.

The CMake target names are stable: `tagcore`, `tagcore_c`, `tagcore_c_static`,
`qcustomplot`, `sensoranalysis`, `sensordata`, `sensorui`, `qtmonitor`,
`qtprogram`, `qtcalibrate`, `btviz`, `sensorviz`, `tagprogram`, `tagmonitor`,
`tagcalibrate`, `tagview`, `flash_tools`.

## Packaging notes

Install and package target selection lives in `host/CMakeLists.txt`.
`host_qt_install_targets` controls which Qt apps are included in the package;
the Rust applications install themselves from their own `CMakeLists.txt`
(macOS bundles assembled by CMake, Windows executables started by the GUI
launcher, Linux executables in `tag_tools/bin`), and `tagcore_c` is installed
once as a shared library on Linux and Windows (statically linked on macOS).
Applications that use QML set `QT_DEPLOY_QML_DIRS` so deployment tooling can
collect the required QML imports and resources; the default build runs
`qml_import_check_<app>` (`cmake/CheckQmlImports.cmake`) for each, which fails
when the Qt install lacks a module those directories import. A new QML import
of a Qt add-on therefore also needs that add-on under `modules:` in
`.github/workflows/release.yml`. On macOS, macdeployqt copies every module
installed under each QML directory an app imports; the install then prunes
the bundle back to what the app needs (`cmake/PruneMacosQml.cmake`, option
`MACOS_PRUNE_UNUSED_QML`) before signing. The Rust applications' third-party
notices (`LICENSES/host/*-THIRD-PARTY-NOTICES.txt`) are regenerated from
`host/Cargo.lock` whenever it changes
(`LICENSES/tools/generate_rust_notices.py`). The retired `compviz` CompassTag
viewer was removed once `sensorviz` handled CompassTag logs; its source is in
git history.

## Maintenance guidelines

- Put tag communication, log writer/reader code, and protocol-facing helpers in
  `libraries/tagcore` (C++) and `libraries/tagcore-rs` (Rust). A change to the
  wire behaviour of one is made in both; the C++ is the reference where they
  disagree ([tagcore-rs design](libraries/tagcore-rs/design/architecture.md)).
- Do not port C++ logic to Rust when it can be reached through `tagcore_c`:
  one writer, one calibration, one schema
  ([proposal](libraries/tagcore/design/proposals/rust-over-cpp-libraries.md)).
  A new C interface gets a module in `tagcore-c-sys` and its own wrapper crate.
- Put UI-free sensor algorithms in `libraries/sensoranalysis`, with the
  arithmetic in its Qt-free `core/`.
- Put reusable Qt widgets in `libraries/sensorui`; put what every web front end
  shares in `ui-kit/`.
- Put one-off application behaviour inside the relevant `applications/*`
  directory until a second app genuinely needs it. In a Rust application every
  rule lives in `core/`; the page in `ui/` only draws.
- Prefer native SQLite APIs over QtSql in shared host code.
- Every Rust item is documented, private items included (`missing_docs` and
  `clippy::missing_docs_in_private_items` warn in every crate); every C++ API
  carries Doxygen. See [AGENTS.md](AGENTS.md) for the checks.
