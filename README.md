---
title: Repository README
type: readme
status: current
summary: Top-level CMake options, prerequisites and build commands for host tools and firmware on Windows, macOS and Linux, and installing a macOS release.
---

See the [repository documentation](https://tag-designs.github.io/software/) for
host/user guides, developer notes, and generated API references. The
[project home](https://tag-designs.github.io/) carries the project overview,
the tag families, the cross-cutting user guides and the system architecture.


# Overview

This repository builds two broad families of software:

| Area | Description |
| --- | --- |
| Host tools | Desktop command-line tools and Qt applications used to communicate with, configure, program, monitor, and visualize tag data. |
| Embedded tools | Firmware and support targets for tags and base boards. |

The top-level CMake build is controlled with cache options:

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_HOST` | `ON` | Build host-side libraries, command-line tools, and support code. |
| `BUILD_QT_APPS` | `ON` | Build Qt GUI applications under `host`. |
| `BUILD_EMBEDDED` | `OFF` on Windows, `ON` elsewhere | Build embedded firmware targets. |
| `BUILD_HOST_DOCS` | `BUILD_QT_APPS` value | Build the host application user guide as part of the default build and install it with host packages. The manual `docs` target is available in top-level builds either way. |
| `BUILD_DEVELOPER_DOCS` | `OFF` | Build the developer documentation portal and API references by default. |

Useful configure combinations:

```
# Host tools and Qt apps only
cmake -S . -B build-host -DBUILD_EMBEDDED=OFF

# Embedded firmware only
cmake -S . -B build-embedded -DBUILD_HOST=OFF -DBUILD_QT_APPS=OFF

# Everything
cmake -S . -B build-all
```


# Host Tools

Host builds require CMake 3.20 or newer, a C++20 compiler, Protobuf, Git,
SQLite 3 development files, and `libusb-1.0` development files. On
non-Windows builds, CMake also requires `pkg-config` to locate `libusb-1.0`.
Qt 6 is required when `BUILD_QT_APPS=ON`. Python 3 with MkDocs Material is
required when `BUILD_HOST_DOCS=ON` or when manually building the `docs` target.
Rust 1.85 or newer (from [rustup](https://rustup.rs)) builds Tag Programmer,
`host/applications/tagprogram-rs`, and Tag Monitor,
`host/applications/tagmonitor-rs`; where `cargo` is not installed they are
skipped (`BUILD_RUST_APPS`), and on Linux they also need the WebKitGTK 4.1
development files.

The Qt host applications use these Qt 6 modules: Core, Gui, Widgets,
Concurrent, PrintSupport, Svg, SvgWidgets, Qml, Quick, QuickWidgets, and
Quick 3D. Quick 3D is imported from QML rather than linked, so CMake does not
ask for it at configure time. Instead each Qt application's build ends with a
check, `qml_import_check_<app>`, that fails if the Qt install lacks a module the
app's QML imports. Where that check cannot find `qmlimportscanner` it warns and
is skipped, and a missing module then shows up only when the view loads.
A minimal CLI-only
host build can disable Qt applications with `-DBUILD_QT_APPS=OFF`.

## Windows Prerequisites

Windows builds assume the Microsoft Visual Studio compiler toolchain.

| Software | Notes |
| --- | --- |
| Visual Studio 2026 Community or newer | Install the Desktop development with C++ workload. The Windows presets use the `Visual Studio 18 2026` generator. |
| CMake 3.20 or newer | Used for configure, build, install, and CPack ZIP packaging. |
| Git | Used by CMake version-generation helpers. |
| vcpkg | Set `VCPKG_ROOT` to the vcpkg root containing `scripts/buildsystems/vcpkg.cmake`. Visual Studio's bundled vcpkg can be used. The repository manifest installs `libusb`, `protobuf`, `sqlite3`, and host `pkgconf`. |
| Qt 6 for MSVC | Install Qt separately. The Windows presets expect `C:/Qt/6.10.3/msvc2022_64`. |
| Python 3 and MkDocs Material | Required when `BUILD_HOST_DOCS=ON` or when manually building the `docs` target. Install with `python -m pip install -r host/docs/requirements.txt`. If the `mkdocs` script is not on `PATH`, CMake can run it as a Python module. |
| Rust 1.85 or newer | For Tag Programmer. Install with [rustup](https://rustup.rs) (the MSVC toolchain, its default). Without `cargo` Tag Programmer is skipped. |

When using Visual Studio's bundled vcpkg from PowerShell, set `VCPKG_ROOT`
before configuring:

```
$env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
```

The preferred Windows preset uses static vcpkg libraries for non-Qt
dependencies and the standard dynamic Qt distribution:

| Setting | Value |
| --- | --- |
| Generator | `Visual Studio 18 2026` |
| Preset | `windows-vcpkg-static` |
| vcpkg triplet | `x64-windows-static-md` from `cmake/vcpkg-triplets` |
| Qt source | Installed Qt at `C:/Qt/6.10.3/msvc2022_64` |
| MSVC runtime | Dynamic runtime, `/MD` and `/MDd` |
| Embedded build | `OFF` |
| Release install directory | `c:/software-build-static-vcpkg-qt/install/tag_tools` |

## Windows Build

Configure and build Release:

```
cmake --preset windows-vcpkg-static
cmake --build --preset windows-vcpkg-static-release
```

Build Debug only when needed:

```
cmake --build --preset windows-vcpkg-static-debug
```

Visual Studio launches Qt apps with the right runtime `PATH` through the
generated `VS_DEBUGGER_ENVIRONMENT` setting. If running Qt apps directly from
the build tree in PowerShell, put the Qt `bin` directory on `PATH` first.
The vcpkg dependencies are linked statically by this preset.

Release build tree:

```
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
c:\software-build-static-vcpkg-qt\Release\bin\qtmonitor.exe
```

Debug build tree:

```
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
c:\software-build-static-vcpkg-qt\Debug\bin\qtmonitor.exe
```

Install and package Release:

```
cmake --install c:/software-build-static-vcpkg-qt --config Release
cmake --build --preset windows-vcpkg-static-package
```

Windows install and packaging are Release-only. The ZIP contains one shared
`tag_tools` directory with Qt application and command-line launcher
executables, the real applications and command-line tools, Qt DLLs, MSVC
runtime DLLs, Qt plugins, and QML runtime files.
Protobuf, SQLite, libusb, Abseil, and related vcpkg dependencies are linked
statically and are not packaged as separate DLLs. The version in the ZIP name
comes from the highest `vX.Y[.Z]` tag reachable from HEAD at configure time,
falling back to 2.0.0 when there is none:

```
UltralightTags-2.0.0-win64/
  tag_tools/
    *.exe
    lib/
      *.exe
      *.dll
      qt.conf
    docs/
    licenses/
    plugins/
    qml/
```

The top-level executables are lightweight launchers that start the same-named
real executable from `lib`. Keeping the real executables beside the DLLs lets
Windows find package-local dependencies with its normal DLL search rules while
keeping DLLs out of the top-level directory. `windeployqt` is configured with
explicit `--libdir`, `--plugindir`, and `--qml-deploy-dir` paths. Translations
are skipped with `--no-translations`.

If CMake is not being run through the preset, pass the Windows dependencies
explicitly:

```
cmake -S . -B build ^
  -DCMAKE_TOOLCHAIN_FILE=c:/users/geoff/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md ^
  -DVCPKG_HOST_TRIPLET=x64-windows ^
  -DVCPKG_OVERLAY_TRIPLETS=c:/Users/geoff/software/cmake/vcpkg-triplets ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64 ^
  -DCMAKE_INSTALL_PREFIX=c:/software-build-static-vcpkg-qt/install ^
  -DBUILD_EMBEDDED=OFF
```

## macOS Prerequisites

| Requirement | Notes |
| --- | --- |
| Xcode or command-line tools | Provides the Apple compiler toolchain. |
| CMake 3.20 or newer | Used for configure, build, install, and packaging. |
| Qt 6 | Required for Qt host applications and `macdeployqt`. |
| `pkg-config` | Used to locate `libusb-1.0` for non-vcpkg builds. |
| vcpkg | Used by the `macos-vcpkg` preset for static non-Qt libraries. The preset expects vcpkg at `$HOME/Software/vcpkg` and Qt at `$HOME/qt/6.10.3/macos`. |
| Homebrew autotools | Required by vcpkg's `libusb` port on macOS: `brew install autoconf autoconf-archive automake libtool`. These are build-only tools, not packaged runtime dependencies. |
| `libusb-1.0` | Install with a package manager or provide a CMake/pkg-config discoverable installation. |
| Protobuf | Install with a package manager or provide a CMake discoverable installation. |
| SQLite 3 | Install development headers/libraries or use the SQLite files from the macOS SDK if your toolchain exposes them to CMake. |
| Git | Used by version-generation helpers. |
| Rust 1.85 or newer | For Tag Programmer. Install with [rustup](https://rustup.rs). Without `cargo` Tag Programmer is skipped. |
| Python 3 and MkDocs Material | Required when `BUILD_HOST_DOCS=ON` or when manually building the `docs` target. Install with `python3 -m pip install -r host/docs/requirements.txt`. |
| `codesign` | Required when `MACOS_SIGN_APPS=ON`. |

Make sure the Qt `bin` directory for the selected Qt version is on `PATH` so
CMake can find `macdeployqt`.

## macOS Build

The preferred packaged host build uses vcpkg for static non-Qt libraries and
the standard dynamic Qt distribution:

```
# Run these from the repository source directory. The preset writes the
# build tree to ~/Build/tag-designs/software-vcpkg-release.
cmake --preset macos-vcpkg
cmake --build --preset macos-vcpkg-package
```

The `macos-vcpkg` configure preset sets the source directory to this checkout
and the build directory to `~/Build/tag-designs/software-vcpkg-release`; the
package build preset then builds the `package` target in that same build tree.
The generated DMG is named from the highest reachable Git tag matching `vX.Y`,
for example `Ultralight-tags-v3.0.dmg` -- and that name is resolved when CMake
*configures*, not when it packages, so the tag has to exist before the configure
step.

A release normally ships the DMG CI builds for the tag, re-signed on this Mac
by `cmake --build <build-dir> --target sign-latest`. To build the release
package here instead, use `host/tools/release-macos.sh vX.Y`, which creates the
tag first for that reason (and pushes it unless given `--no-push`; the tag only
has to exist locally for the name), then runs the two commands above and
verifies the signatures inside the resulting DMG. See
[Releasing the host tools](docs/release/release-procedure.md#3-releasing-the-host-tools).

This keeps Protobuf, SQLite, libusb, Abseil, and related vcpkg dependencies out
of the app bundles as separate dylibs. Qt remains dynamic and is deployed with
`macdeployqt`.

Before using this preset, install the host autotools required by vcpkg's
`libusb` build:

```
brew install autoconf autoconf-archive automake libtool
```

For a manual package build without the preset, choose the source and build
directories explicitly:

```
cmake -S . -B build-package \
  -DBUILD_QT_APPS=ON \
  -DMACOS_CODE_SIGN_IDENTITY="Indiana University (5J69S77A7G)"
```

Build the applications and create the DMG:

```
cmake --build build-package --parallel
cmake --build build-package --target package
```

The macOS package flow runs `macdeployqt` during package staging, including
the QML import directories registered on each Qt target with
`QT_DEPLOY_QML_DIRS`, copies the curated Qt plug-ins, signs the final app
bundles, then CPack creates the DragNDrop DMG. These package-related options
default to `ON`:

```
MACOS_SIGN_APPS
MACOS_USE_MACDEPLOYQT_DEPLOYMENT
MACOS_DEPLOY_APPSTORE_COMPLIANT
MACOS_DEPLOY_QT_PLUGINS_MANUALLY
MACOS_FIXUP_BUNDLE_DEPENDENCIES
MACOS_PRUNE_UNUSED_QML
```

`MACOS_PRUNE_UNUSED_QML` removes the QML modules and Qt frameworks an app does
not import, which `macdeployqt` would otherwise copy wholesale -- including
several modules licensed only under the GPL.

To verify a packaged app:

```
codesign --verify --deep --verbose path/to/App.app
spctl -a -vvv path/to/App.app
```

To override or disable signing:

```
cmake -S . -B build-package \
  -DBUILD_QT_APPS=ON \
  -DMACOS_CODE_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"

cmake -S . -B build-package \
  -DBUILD_QT_APPS=ON \
  -DMACOS_SIGN_APPS=OFF
```

## Releases

Host tools release on `vX.Y` tags and tag firmware on `fw-vX.Y` tags. What CI
builds for each, how a firmware image is qualified, how a field tag is
programmed from a release and recorded, and how the signed macOS package is
cut are in [Releasing Tag Firmware and Host Tools](docs/release/release-procedure.md).
Installing a macOS release is covered next.

## Installing a macOS Release

Drag `tag_tools` out of the mounted DMG — to Applications, or anywhere else
you like — and open the apps from there.

Releases after v3.0.1 are signed with the Indiana University Developer ID
certificate and notarized by Apple. The first time you open one, macOS says it
was downloaded from the internet, asks whether to open it, and offers to
accept everything that came in the same DMG. Accept that and the other apps
open without asking.

The command-line tools (`tag-info`, `tag-dwnld` and the rest, and
`dataprocessing`) are in `tag_tools/bin`. Run them from a terminal, or put the
directory on your `PATH`, for example in `~/.zshrc`:

```
export PATH="/Applications/tag_tools/bin:$PATH"
```

They are notarized too, but a bare executable cannot carry its ticket, so
macOS looks it up with Apple the first time each tool runs; do that once with
a network connection. Releases
v3.2 and earlier shipped each tool as an app instead, run as
`tag_tools/tag-info.app/Contents/MacOS/tag-info`.

### Releases v3.0.1 and earlier

These are signed but not notarized, so macOS blocks each app on first launch
and has to be told once, per app, to allow them. Run them from the folder you
dragged out: the DMG is a read-only volume, so nothing below works while the
apps are still on it.

#### The quick way, if you are comfortable in a terminal

Clearing the quarantine attribute on the extracted folder covers every app at
once, and they then open normally with no prompt at all:

```
xattr -dr com.apple.quarantine /path/to/tag_tools
```

Quarantine is what makes macOS demand notarization; without it the Developer ID
signature is accepted on its own. This is one command instead of thirteen trips
through System Settings.

#### The GUI way, per app

1. Double-click the app. macOS refuses it — **this step is required**, because
   the override in step 3 does not appear until something has been blocked.
2. Open **System Settings → Privacy & Security** and scroll to **Security**.
   There is a line naming the app that was blocked.
3. Click **Open Anyway** and authenticate.
4. Open the app again and confirm.

macOS then remembers that app. Repeat for each app you use.

Older instructions — including earlier versions of this file — say to
Control-click the app and choose **Open**. That route was removed in macOS 15;
on current systems the menu has an Open item that behaves no differently from a
double-click, so it looks like nothing happened. Use System Settings.

### If an app reports that it "is damaged and can't be opened"

That is a different problem, and neither notarization nor clearing quarantine
fixes it. It means the bundle has no valid signature — true of the DMG that CI builds, where the
Developer ID certificate is not available. Those DMGs are workflow artifacts,
never attached to a release. Check that you downloaded from the release page
rather than from an Actions run.

## Installing a Linux Release

Releases after v3.2 carry a Linux package for x86_64,
`UltralightTags-X.Y-linux-x86_64.tar.gz`. It runs on Ubuntu 24.04, Debian 13
and other distributions of the same age or newer (glibc 2.39), and needs no
Qt or anything else installed beyond what a desktop already has. Tag
Programmer (`tagprogram`) and Tag Monitor (`tagmonitor`) draw their windows
with WebKitGTK 4.1, which GNOME
desktops have; elsewhere install it with `sudo apt install libwebkit2gtk-4.1-0`.
Unpack it anywhere:

```
tar -xzf UltralightTags-X.Y-linux-x86_64.tar.gz
export PATH="$PWD/UltralightTags-X.Y-linux-x86_64/tag_tools/bin:$PATH"
```

The command line tools and the applications are all in `tag_tools/bin`.

Linux lets an ordinary user open a USB device only when a udev rule allows
it, so install the one in the package once, or every tool fails with
`LIBUSB_ERROR_ACCESS`:

```
sudo cp tag_tools/udev/70-ultralight-tags.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Unplug and replug the tag base afterwards. The same rule serves a build from
source; it is `host/linux/70-ultralight-tags.rules` in this repository.

The applications run natively on Wayland and on X11. On an X11 session, if
one reports that the xcb platform plugin could not be loaded, install
`libxcb-cursor0`. To run one through XWayland on a Wayland session instead,
set `QT_QPA_PLATFORM=xcb`.

## Linux Prerequisites

The preferred Linux build is the one releases are made with: Qt 6.10.3 from
the Qt installer, and vcpkg for static non-Qt libraries, like the Windows and
macOS presets. It runs on x86_64 Ubuntu 24.04, Debian 13 or newer.

| Requirement | Notes |
| --- | --- |
| C++20 compiler, `make`, CMake 3.20 or newer, Git | `build-essential cmake git` |
| Qt 6.10.3 | From the Qt online installer, or `aqt install-qt linux desktop 6.10.3 linux_gcc_64 -m qtquick3d qtshadertools qtquicktimeline`. The `linux-vcpkg` preset expects it at `$HOME/Qt/6.10.3/gcc_64`. A different version builds with a warning; see `HOST_QT_VERSION` below. |
| vcpkg | Clone and bootstrap it at `$HOME/Software/vcpkg`, as the preset expects. The repository manifest installs `libusb`, `protobuf`, `sqlite3` and host `pkgconf`. |
| vcpkg's build needs | `curl zip unzip tar pkg-config`, and autotools and the libudev headers for its `libusb` port: `autoconf autoconf-archive automake libtool libudev-dev`. |
| Qt's build headers and the libraries it links | `libgl1-mesa-dev libegl-dev libxkbcommon-dev`, and `libglib2.0-0t64 libdbus-1-3 libfontconfig1`, which a desktop already has |
| Python 3 and MkDocs Material | Required when `BUILD_HOST_DOCS=ON` or when manually building the `docs` target. Install with `python3 -m pip install -r host/docs/requirements.txt`. |
| Rust 1.85 or newer, and WebKitGTK 4.1 | For Tag Programmer: Rust from [rustup](https://rustup.rs) (the distributions' `cargo` is too old on Ubuntu 24.04), and `libwebkit2gtk-4.1-dev`. Without `cargo` Tag Programmer is skipped. |

On Debian or Ubuntu:

```
sudo apt install build-essential cmake git pkg-config curl zip unzip tar \
  autoconf autoconf-archive automake libtool libudev-dev \
  libgl1-mesa-dev libegl-dev libxkbcommon-dev libglib2.0-0t64 libdbus-1-3 libfontconfig1 \
  libwebkit2gtk-4.1-dev
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
git clone https://github.com/microsoft/vcpkg ~/Software/vcpkg
~/Software/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

## Linux Build

```
cmake --preset linux-vcpkg
cmake --build --preset linux-vcpkg-release
```

The build tree is `~/Build/tag-designs/software-vcpkg-release`. To make the
release tarball and test it as CI does, in clean Ubuntu and Debian containers
under Wayland and X11 (needs Docker):

```
cmake --build --preset linux-vcpkg-package
host/tools/linux-build-check.sh --package \
  ~/Build/tag-designs/software-vcpkg-release/UltralightTags-*-linux-x86_64.tar.gz
```

`cmake --install` writes the same relocatable `tag_tools` directory the
package holds, carrying its own Qt.

### With the distribution's packages instead

The host tools also build against a distribution's own Qt and libraries. This
is quicker to set up, but it is not how releases are built: the Qt is the
distribution's (6.8 on Debian 13, 6.4 on Ubuntu 24.04, which is untested), so
configure warns that it is not `HOST_QT_VERSION`, and an install uses that Qt
in place rather than carrying its own.

```
sudo apt install build-essential cmake git pkg-config \
  libusb-1.0-0-dev libprotobuf-dev protobuf-compiler libsqlite3-dev \
  qt6-base-dev qt6-svg-dev qt6-declarative-dev qt6-quick3d-dev \
  qml6-module-qtquick3d qml6-module-qtquick3d-assetutils qml6-module-qtquick3d-helpers \
  libwebkit2gtk-4.1-dev curl
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
cmake -S . -B build-host -DBUILD_EMBEDDED=OFF
cmake --build build-host --parallel
```

Package names vary between releases. If one of the Quick 3D QML modules is
missing, the build's `qml_import_check_qtcalibrate` step fails and names it.

To check these instructions without a Linux machine, or against a distribution
other than your own, `host/tools/linux-build-check.sh` runs them in a throwaway
Docker container (OrbStack or Docker Desktop): it installs the packages above
into a stock Debian image, builds against the distribution's Qt, and leaves
nothing behind but the base image. `--image ubuntu:24.04` picks another
distribution; `--help` lists the rest.

## Host Cache Variables

| Variable | Default | Description |
| --- | --- | --- |
| `CMAKE_INSTALL_PREFIX` | build `install` directory | Install and package staging location. |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | `13.0` | Minimum macOS version for host software. Qt 6.10 is built for macOS 13. |
| `HOST_MKDOCS_COMMAND` | auto-detected | Command used to run MkDocs. Use a semicolon-separated CMake command list such as `python;-m;mkdocs` or `C:/Path/To/python.exe;-m;mkdocs` when `mkdocs` is not directly on `PATH`. |
| `Qt6_DIR` or `CMAKE_PREFIX_PATH` | platform dependent | Use when CMake cannot find Qt automatically. |
| `BUILD_RUST_APPS` | `ON` where `cargo` is found | Build Tag Programmer (`host/applications/tagprogram-rs`) and Tag Monitor (`host/applications/tagmonitor-rs`) with cargo and install them beside the Qt applications, with `tagcore_c`, through which Tag Monitor writes its downloads. The release workflow sets it, so a missing `cargo` fails there. |
| `HOST_QT_VERSION` | `6.10.3` | The Qt releases are built with. Configuring against another Qt warns, and fails under `-DREPRODUCIBLE_BUILD=ON`, which the release workflow sets. Empty skips the check. |
| `MACOS_CODE_SIGN_IDENTITY` | `Developer ID Application: Indiana University (5J69S77A7G)` | Signing identity used for macOS bundles. |
| `MACOS_CODE_SIGN_ENTITLEMENTS` | empty | Optional entitlements plist used when signing app bundles. |
| `MACOS_MACDEPLOYQT_EXTRA_OPTIONS` | empty | Extra options passed to `macdeployqt`. |


# Embedded Tools

Embedded builds produce firmware and programming targets for tags and base
boards.

For an overview of the embedded source tree and how `bases`, `boards`,
`proto-c`, and `tags` relate to each other, see
[embedded/design/source-layout.md](embedded/design/source-layout.md).

## Embedded Prerequisites

| Requirement | Notes |
| --- | --- |
| CMake 3.20 or newer | Required by the top-level build and embedded nanopb helper targets. |
| Native C and C++ compiler | CMake's top-level `project()` enables C and C++, so configure needs a native compiler even for an embedded-only build. |
| Python 3 | Needed by the default configure, which builds the pinned `config-gen` virtual environment from `embedded/proto-c/requirements.txt` (with network access the first time) unless `-DREGENERATE_SOURCES=OFF`, and to regenerate a board. The C++ Protobuf library is needed only when `BUILD_HOST` is on. |
| Git | Used by version-generation helpers. |
| Arm GNU Toolchain | Provides `arm-none-eabi-gcc`; it must be on `PATH`. Version 14.2.1 is the expected one; a different version warns, and fails under `-DREPRODUCIBLE_BUILD=ON`. Configure with `-DARM_TOOLCHAIN_VERSION=` to record the version without checking it. |
| ChibiOS | Tracked as the `ChibiOS` git submodule on branch `stable_21.11.x`. |
| nanopb generator and `protoc` | Needed by the default configure: the prototype proto variants do not commit their generated sources, so configure fails without a generator (set `NANOPB_ROOT`), and it fails without a `protoc` (normally the one the nanopb distribution ships). Not needed with `-DREGENERATE_SOURCES=OFF`. The generated `.pb.c`/`.pb.h` for the distributed tags are committed, and the runtime is vendored in the repository. |
| Java runtime | Required by `fmpp`. |
| `fmpp` | Needed by the default configure: the boards of prototype tags and bases do not commit their generated files, so configure fails without `fmpp`. Not needed with `-DREGENERATE_SOURCES=OFF`. |
| `make` | Required for ChibiOS-based firmware builds. |
| STM32CubeProgrammer | Optional for building, required for generated download/DFU targets. |

Before configuring an embedded build, initialize the ChibiOS submodule. This is
required after a fresh clone, and may also be needed after pulling changes that
update the submodule pointer:

```
git submodule update --init --recursive ChibiOS
```

If `ChibiOS` is missing or uninitialized, CMake will stop during embedded
configuration and ask you to initialize the submodule. A developer can still
override the dependency explicitly with `-DCHIBIOS_DIR=/path/to/ChibiOS`, but
normal builds should use the checked-out submodule so every platform builds
against the same ChibiOS revision.

Useful environment variables:

```
NANOPB_ROOT=/path/to/nanopb
PATH=/path/to/gcc-arm/bin:/path/to/fmpp/bin:$PATH
```

## Embedded Build

Configure embedded-only:

```
cmake -S . -B build-embedded -DBUILD_HOST=OFF -DBUILD_QT_APPS=OFF -DBUILD_EMBEDDED=ON
```

This default configure (`REGENERATE_SOURCES=AUTO`) needs every generator in the
table above: `fmpp` and Java, the nanopb generator, `protoc`, and Python 3 with
network access for the first `config-gen` virtual environment. On a machine
without them, build the distributed tags from their committed generated sources
instead:

```
cmake -S . -B build-embedded -DBUILD_HOST=OFF -DBUILD_QT_APPS=OFF -DBUILD_EMBEDDED=ON \
  -DREGENERATE_SOURCES=OFF
cmake --build build-embedded --target distributed_firmware
```

That needs only the Arm toolchain, `make`, CMake, a native compiler and the
ChibiOS submodule. It configures the whole tree, but only targets whose board
and proto variant both commit their generated files can be built; asking for
any other (a prototype tag or a base) fails with a message saying to use
`AUTO`. See
[Tag Firmware Build Reproducibility](docs/build/firmware-reproducibility.md#what-a-build-actually-needs).

Build tag firmware:

```
cmake --build build-embedded --target BitTag
cmake --build build-embedded --target PresTag
```

Build and download tag firmware:

```
cmake --build build-embedded --target BitTag-download
cmake --build build-embedded --target PresTag-download
```

Download targets prefer the tag programmer base ST-LINK PID (`0483:3748`) and
fall back to other attached ST-LINK programmers, such as STLINK-V3PWR
(`0483:3757`). If more than one programmer is connected, select one explicitly:

```
STM32_PROGRAMMER_PROBE=serial:<stlink-serial> cmake --build build-embedded --target BitTag-download
STM32_PROGRAMMER_PROBE=pid:0483:3757 cmake --build build-embedded --target BitTag-download
```

Build base-board targets:

```
cmake --build build-embedded --target bittag-base-jlcpcb-v3
cmake --build build-embedded --target bittag-base-jlcpcb-v3-dfu
cmake --build build-embedded --target tag-breakout-base-jlcpcb32-v1
cmake --build build-embedded --target tag-breakout-base-jlcpcb32-v1-dfu
```

The `-dfu` targets attempt to program the board.

Install built firmware artifacts:

```
cmake --install build-embedded
```

The install step copies the `.elf`, `.hex`, `.bin`, `.dmp`, and `.list` files
of the tags marked `DISTRIBUTE`, with each one's build manifest, to
`share/UltralightTags/firmware/<target>/`; external loaders marked `DISTRIBUTE`
go to `share/UltralightTags/loaders/<loader>/`. Prototype tags and bases are not
installed. The rules are `OPTIONAL`, so an artifact that was not built is
skipped silently; build `distributed_firmware` first.

## Embedded nanopb Options

How nanopb options are layered and how to add a tag protocol target are in
[embedded/design/source-layout.md](embedded/design/source-layout.md#proto-c).
What is committed and when it is regenerated is in
[Tag Firmware Build Reproducibility](docs/build/firmware-reproducibility.md).

## Programming Embedded Hardware

Install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
to use generated download and DFU targets.

Example SWD command:

```
~/Software/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe -c port=SWD mode=UR -d ch.elf -v -g 0x08000000
```

Useful options:

| Option | Meaning |
| --- | --- |
| `port=SWD` | Use the SWD driver. |
| `mode=UR` | Attach under reset. |
| `-g 0x08000000` | Execute program after completion. |
| `-v` | Verify. |
| `-vb 3` | Verbose logging for debugging. |

DFU discovery:

```
STM32_Programmer_CLI.exe -l usb
```

DFU programming:

```
~/Software/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=usb1 -d build/ch.elf
```

## License

The project's own code is licensed under the [MIT License](LICENSE), Copyright
(c) 2018-2026 The Trustees of Indiana University. The following programs link
GPL-licensed components and are therefore distributed under the GNU GPL,
version 3:
- **The tag and base-board firmware:** ChibiOS/RT.
- **`sensorviz` and `btviz`:** QCustomPlot.
- **`qtcalibrate`:** Qt Quick 3D.

[LICENSES/](LICENSES/README.md) explains this, and lists every third-party
component with links to its license:
- **Host tools:** [LICENSES/host](LICENSES/host/README.md).
- **Firmware and loaders:** [LICENSES/embedded](LICENSES/embedded/README.md).

The packages and the firmware archive carry the same directories.
