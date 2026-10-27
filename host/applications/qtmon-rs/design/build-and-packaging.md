---
type: design
status: current
summary: How Tag Monitor in Rust is built and shipped - the cargo workspace, the CMake target that wraps cargo and links the C++ log writer (tagcore_c), the per-platform install layout, the CI build, the third-party notices, and what it shares with Tag Programmer's packaging.
---

# Building and packaging Tag Monitor (Rust)

Tag Monitor in Rust is built by cargo and shipped inside the host tools
package beside the Qt applications as `tagmonitor`, exactly as
[Tag Programmer](../../tagprogram-rs/design/build-and-packaging.md) is. This
page covers what is particular to it. The page staging in `app/build.rs`, the
user-guide search, the macOS bundle and signing, and the CI Rust steps are
Tag Programmer's, and its page explains them.

The one difference that shapes the packaging: **Download Data... writes
SQLite through the C++ writer**, the same `SqliteTagLogWriter` qtmonitor and
`tag-dwnld` use, so each download is the file they would write. The writer
is reached through `tagcore_c`, the C interface CMake builds from the C++
libraries ([tagcore_c](../../../libraries/tagcore-c/README.md)), and the
`taglogwriter` crate ([taglogwriter-rs](../../../libraries/taglogwriter-rs/README.md)).
A CMake build therefore builds `tagcore_c` before cargo, and the packages
carry it.

## The workspace

| Crate | Path | Role |
| --- | --- | --- |
| `tagmonitor` | `app/` | The Tauri shell, the only binary |
| `qtmon-core` | `core/` | Model and engine; no UI toolkit, unit-testable |
| `tagcore` | `../../libraries/tagcore-rs` | The monitor protocol over ST-LINK (path dependency) |
| `taglogwriter` | `../../libraries/taglogwriter-rs` | The C++ SQLite writer, through `tagcore_c` (optional path dependency) |

The `sqlitelog` feature (of both `qtmon-core` and `tagmonitor`) brings in
`taglogwriter`. Without it the crates build with cargo alone, and Download
refuses with a message; with it, `TAGCORE_C_LIB_DIR` must name the
directory holding the library. The CMake build always turns it on. The
workspace root is `host/applications/qtmon-rs`; its `Cargo.lock` is
committed and release builds use `--locked`.

## The CMake target

`CMakeLists.txt` is included from `host/CMakeLists.txt` when
`BUILD_RUST_APPS` is on, which also builds `tagcore_c` (whatever
`BUILD_TAGCORE_C` says). The target `tagmonitor`:

- depends on the library target: `tagcore_c_static` on macOS,
  `tagcore_c` elsewhere;
- runs `cargo build --release --locked -p tagmonitor --features sqlitelog`
  every time, leaving the up-to-date decision to cargo;
- sets `CARGO_TARGET_DIR` to `<build>/host/applications/qtmon-rs/cargo`,
  and `TAGCORE_C_LIB_DIR` to `<build>/<Config>/lib`, where both library
  targets put their output;
- on Linux, sets `TAGCORE_C_RPATH=$ORIGIN/../lib`, which `app/build.rs`
  makes the program's run-time search path, so the installed program finds
  the library the package installs beside the others;
- on macOS, passes `MACOSX_DEPLOYMENT_TARGET`.

### How `tagcore_c` is linked

| Platform | Link | Why |
| --- | --- | --- |
| macOS | static: `libtagcore_c.a`, inside the executable | Each application is its own bundle and would carry its own copy of a shared library anyway. The program links only system libraries, as the release procedure checks. |
| Linux | shared: `libtagcore_c.so` | One copy serves the install directory, as the decision for `tag-dwnld` set out. |
| Windows | shared: `tagcore_c.dll` | As Linux. |

These are `taglogwriter`'s defaults (`TAGCORE_C_LINK` overrides them). The
library holds the writer, libprotobuf with abseil, and SQLite, with every
symbol but the `tlw_*` functions hidden, so nothing it carries clashes with
the program's own crates. The rpath is added only for the shared link, so a
macOS build carries no build-tree path.

### Install layout

| Platform | Installed as | Notes |
| --- | --- | --- |
| macOS | `tag_tools/tagmonitor.app` | Bundle assembled by CMake: binary, `Info.plist` from `cmake/Info.plist.in`, `icon.icns`; signed by `macos_codesign_bundle`. |
| Windows | `tag_tools/tagmonitor.exe` (launcher), `tag_tools/lib/tagmonitor.exe`, `tag_tools/lib/tagcore_c.dll` | `tagcore_c.dll` uses the MSVC runtime DLLs, which the package keeps in `tag_tools/lib`, and Windows finds a program's DLLs only beside it. So the program lives there and the GUI launcher starts it, as for the Qt applications. Uses the WebView2 runtime. |
| Linux | `tag_tools/bin/tagmonitor`, `tag_tools/lib/libtagcore_c.so` | Links the system WebKitGTK 4.1. |

The bundle is named `tagmonitor.app`, beside qtmonitor's `qtmonitor.app`;
the window and the bundle's display name are "Tag Monitor". The app's
settings (recent configuration files) are in the platform app config
directory under the identifier `com.tagdesigns.tagmonitor`.

`Info.plist` declares the Documents, Desktop and Downloads usage strings,
since configurations and downloads are opened and saved wherever the user
chooses.

## The user guide

Help > User Guide opens the manual page `apps/tagmonitor.html`, found the
way Tag Programmer finds its own, or the published manual.

## CI

`.github/workflows/release.yml` builds all three packages with
`-DBUILD_RUST_APPS=ON`, so Tag Monitor and `tagcore_c` are in every package.
The Rust cache covers both Rust workspaces. The package checks:

- `host/tools/linux-package-test.sh` checks that `tagmonitor` starts and
  stays up in clean Ubuntu and Debian containers (it finds no base there and
  says so);
- the [release procedure](../../../../docs/release/release-procedure.md)
  expects seven app lines.

## Third-party notices

`LICENSES/host/tagmonitor-THIRD-PARTY-NOTICES.txt` lists the crates compiled
into the program, generated as Tag Programmer's is. **Regenerate it whenever
`Cargo.lock` changes**:

```sh
LICENSES/tools/generate_rust_notices.py host/applications/qtmon-rs \
    tagmonitor > LICENSES/host/tagmonitor-THIRD-PARTY-NOTICES.txt
```

The C++ code inside `tagcore_c` (protobuf, abseil, SQLite, libusb) is the
code the Qt applications already ship, and its notices are in
`LICENSES/host/third-party/`.

## Checking a build

```sh
cmake --build <build-dir> --target tagmonitor
cmake --install <build-dir> --prefix /tmp/tm-install
otool -L /tmp/tm-install/tag_tools/tagmonitor.app/Contents/MacOS/tagmonitor   # macOS: system libraries only
```

`core/examples/session_check.rs` then runs a whole tag cycle, the download
included, through the engine (it changes the tag; see its header).
