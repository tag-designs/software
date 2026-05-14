See [Documentation](https://geoffreymbrown.github.io/ultralight-tags/) to explore this project and for complete build instructions.


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
| `BUILD_QT_APPS` | `ON` | Build Qt GUI applications under `software/host`. |
| `BUILD_EMBEDDED` | `OFF` on Windows, `ON` elsewhere | Build embedded firmware targets. |

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
required when `BUILD_HOST_DOCS=ON`.

The Qt host applications use these Qt 6 modules: Core, Gui, Widgets,
PrintSupport, Svg, SvgWidgets, Qml, Quick, and QuickWidgets. A minimal CLI-only
host build can disable Qt applications with `-DBUILD_QT_APPS=OFF`.

## Windows Prerequisites

Windows builds assume the Microsoft Visual Studio compiler toolchain.

| Software | Notes |
| --- | --- |
| Visual Studio 2026 Community or newer | Install the Desktop development with C++ workload. The Windows presets use the `Visual Studio 18 2026` generator. |
| CMake 3.20 or newer | Used for configure, build, install, and CPack ZIP packaging. |
| Git | Used by CMake version-generation helpers. |
| vcpkg | Set `VCPKG_ROOT` to the vcpkg root containing `scripts/buildsystems/vcpkg.cmake`. Visual Studio's bundled vcpkg can be used. The repository manifest installs `libusb`, `protobuf`, `sqlite3`, and host `pkgconf`. |
| Qt 6 for MSVC | Install Qt separately. The Windows presets expect `C:/Qt/6.10.2/msvc2022_64`. |
| Python 3 and MkDocs Material | Required when `BUILD_HOST_DOCS=ON`. Install with `py -m pip install -r software/host/docs/requirements.txt`. |

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
| Qt source | Installed Qt at `C:/Qt/6.10.2/msvc2022_64` |
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
$env:PATH = "C:\Qt\6.10.2\msvc2022_64\bin;$env:PATH"
c:\software-build-static-vcpkg-qt\Release\bin\qtmonitor.exe
```

Debug build tree:

```
$env:PATH = "C:\Qt\6.10.2\msvc2022_64\bin;$env:PATH"
c:\software-build-static-vcpkg-qt\Debug\bin\qtmonitor.exe
```

Install and package Release:

```
cmake --install c:/software-build-static-vcpkg-qt --config Release
cmake --build --preset windows-vcpkg-static-package
```

Windows install and packaging are Release-only. The ZIP contains one shared
`tag_tools` directory with Qt application launcher executables, the real Qt
applications, Qt DLLs, MSVC runtime DLLs, Qt plugins, and QML runtime files.
The command-line tools are built but not included in the install package.
Protobuf, SQLite, libusb, Abseil, and related vcpkg dependencies are linked
statically and are not packaged as separate DLLs:

```
UltralightTags-2.0.0-win64/
  tag_tools/
    *.exe
    lib/
      *.exe
      *.dll
      qt.conf
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
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/msvc2022_64 ^
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
| vcpkg | Used by the `macos-vcpkg` preset for static non-Qt libraries. The preset expects `/Users/geobrown/Software/vcpkg`. |
| Homebrew autotools | Required by vcpkg's `libusb` port on macOS: `brew install autoconf autoconf-archive automake libtool`. These are build-only tools, not packaged runtime dependencies. |
| `libusb-1.0` | Install with a package manager or provide a CMake/pkg-config discoverable installation. |
| Protobuf | Install with a package manager or provide a CMake discoverable installation. |
| SQLite 3 | Install development headers/libraries or use the SQLite files from the macOS SDK if your toolchain exposes them to CMake. |
| Git | Used by version-generation helpers. |
| Python 3 and MkDocs Material | Required when `BUILD_HOST_DOCS=ON`. Install with `python3 -m pip install -r software/host/docs/requirements.txt`. |
| `codesign` | Required when `MACOS_SIGN_APPS=ON`. |

Make sure the Qt `bin` directory for the selected Qt version is on `PATH` so
CMake can find `macdeployqt`.

## macOS Build

The preferred packaged host build uses vcpkg for static non-Qt libraries and
the standard dynamic Qt distribution:

```
cmake --preset macos-vcpkg
cmake --build --preset macos-vcpkg-package
```

This keeps Protobuf, SQLite, libusb, Abseil, and related vcpkg dependencies out
of the app bundles as separate dylibs. Qt remains dynamic and is deployed with
`macdeployqt`.

Before using this preset, install the host autotools required by vcpkg's
`libusb` build:

```
brew install autoconf autoconf-archive automake libtool
```

Configure a package build:

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
```

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

## Linux Prerequisites

| Requirement | Notes |
| --- | --- |
| C++20 compiler | GCC or Clang. |
| CMake 3.20 or newer | Used for configure and build. |
| Qt 6 | Required when `BUILD_QT_APPS=ON`. |
| `pkg-config` | Used to locate `libusb-1.0`. |
| `libusb-1.0` | Install development headers and libraries. |
| Protobuf | Install `protoc` and development libraries. |
| SQLite 3 | Install development headers and libraries, for example `libsqlite3-dev`. |
| Git | Used by version-generation helpers. |
| Python 3 and MkDocs Material | Required when `BUILD_HOST_DOCS=ON`. Install with `python3 -m pip install -r software/host/docs/requirements.txt`. |

Example Debian/Ubuntu package set for host builds:

```
sudo apt install build-essential cmake git pkg-config \
  libusb-1.0-0-dev libprotobuf-dev protobuf-compiler libsqlite3-dev
```

For Qt applications on Debian/Ubuntu, also install Qt 6 development packages
that provide the modules listed above, commonly:

```
sudo apt install qt6-base-dev qt6-svg-dev qt6-declarative-dev
```

## Linux Build

```
cmake -S . -B build-host -DBUILD_EMBEDDED=OFF
cmake --build build-host --parallel
```

Set `CMAKE_PREFIX_PATH`, `Qt6_DIR`, or `PKG_CONFIG_PATH` if Qt or libusb is not
installed in a standard location.

## Host Cache Variables

| Variable | Default | Description |
| --- | --- | --- |
| `CMAKE_INSTALL_PREFIX` | build `install` directory | Install and package staging location. |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | `12.3` | Minimum macOS version for host software. |
| `BUILD_HOST_DOCS` | `BUILD_QT_APPS` value | Build and install the host application user guide from `software/host/docs`. |
| `Qt6_DIR` or `CMAKE_PREFIX_PATH` | platform dependent | Use when CMake cannot find Qt automatically. |
| `MACOS_CODE_SIGN_IDENTITY` | `Developer ID Application: Indiana University (5J69S77A7G)` | Signing identity used for macOS bundles. |
| `MACOS_CODE_SIGN_ENTITLEMENTS` | empty | Optional entitlements plist used when signing app bundles. |
| `MACOS_MACDEPLOYQT_EXTRA_OPTIONS` | empty | Extra options passed to `macdeployqt`. |


# Embedded Tools

Embedded builds produce firmware and programming targets for tags and base
boards.

## Embedded Prerequisites

| Requirement | Notes |
| --- | --- |
| CMake 3.20 or newer | Required by the top-level build and embedded nanopb helper targets. |
| Native C++20 compiler | Builds host-side generation helpers used by embedded protocol targets. |
| Protobuf | Install `protoc` and development libraries; the embedded build still configures shared protocol targets. |
| Git | Used by version-generation helpers. |
| Arm GNU Toolchain | Provides `arm-none-eabi-gcc`; it must be on `PATH`. |
| ChibiOS | Set `CHIBIOS_DIR`, or place the tree at `ChibiOS` in the repository root. |
| nanopb | Set `NANOPB_ROOT`, or place the tree at `nanopb` in the repository root. |
| Java runtime | Required by `fmpp`. |
| `fmpp` | Required for board file generation; it must be on `PATH`. |
| `make` | Required for ChibiOS-based firmware builds. |
| STM32CubeProgrammer | Optional for building, required for generated download/DFU targets. |

Useful environment variables:

```
CHIBIOS_DIR=/path/to/ChibiOS
NANOPB_ROOT=/path/to/nanopb
PATH=/path/to/gcc-arm/bin:/path/to/fmpp/bin:$PATH
```

## Embedded Build

Configure embedded-only:

```
cmake -S . -B build-embedded -DBUILD_HOST=OFF -DBUILD_QT_APPS=OFF -DBUILD_EMBEDDED=ON
```

Build tag firmware:

```
cmake --build build-embedded --target BitTagv6
cmake --build build-embedded --target BitTagv5
cmake --build build-embedded --target BitTagNucleo
```

Build and download tag firmware:

```
cmake --build build-embedded --target BitTagv6-download
```

Build base-board targets:

```
cmake --build build-embedded --target bittag-base-jlcpcb-v3
cmake --build build-embedded --target bittag-base-jlcpcb-v3-dfu
cmake --build build-embedded --target tag-breakout-base-jlcpcb32-v1
cmake --build build-embedded --target tag-breakout-base-jlcpcb32-v1-dfu
```

The `-dfu` targets attempt to program the board.

## Embedded nanopb Options

Embedded protocol C files are generated from the host `.proto` files using
nanopb. Each embedded tag protocol target is created with `add_nanopb_target`
under `software/embedded/proto-c`.

Nanopb options are split into two layers:

| File | Purpose |
| --- | --- |
| `software/embedded/proto-c/default-options/tag.options` | Shared defaults for `tag.proto`. |
| `software/embedded/proto-c/default-options/tagdata.options` | Shared defaults for `tagdata.proto`. |
| `software/embedded/proto-c/<tag>-proto-c/tag.override.options` | Tag-specific overrides for `tag.proto`. |
| `software/embedded/proto-c/<tag>-proto-c/tagdata.override.options` | Tag-specific overrides for `tagdata.proto`. |

At build time, CMake combines the default file and the tag override file into
the conventional nanopb file names in the build tree:

```
<build>/software/embedded/proto-c/<tag>-proto-c/tag.options
<build>/software/embedded/proto-c/<tag>-proto-c/tagdata.options
```

To add a new embedded tag protocol target:

1. Create `software/embedded/proto-c/<tag>-proto-c/CMakeLists.txt`.
2. Call `add_nanopb_target(<tag>)`.
3. Add `tag.override.options`, `tagdata.override.options`, and `default-config.json`.
4. Add the subdirectory from `software/embedded/proto-c/CMakeLists.txt`.
5. Reference the generated interface target as `<tag>_proto` from the embedded firmware target.

## Programming Embedded Hardware

Install [STM32CubeProgrammer](https://wiki.st.com/stm32mpu/wiki/STM32CubeProgrammer)
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


# Obsolete Instructions

These notes are retained for review. Some may contain useful historical context,
but they are not the preferred build path.

## Building Qt Locally

No longer building Qt; use the distributed Qt version instead.

Reference:

```
https://doc.qt.io/qt-6/macos-building.html
```

Old macOS universal Qt configure note:

```
./configure -- -DCMAKE_OSX_ARCHITECTURES="x86_64h;arm64"
```

Old MacPorts notes:

```
sudo port install libusb-1.0
# sudo port install qt6
```

## Old Linux Protobuf Build Notes

```
git clone https://github.com/google/protobuf.git
cd protobuf
configure
make
sudo make install
mkdir build
cd build
cmake ../cmake
make
sudo make install
```

## Old Tool Checklist

Host and embedded:

```
cmake
protobuf -- protoc, libprotobuf, python (pip install protobuf)
nanopb
python
```

Host only:

```
Qt5
libusb
vcpkg (windows)
```

Embedded only:

```
chibios
fmpp
stm32 programmer cli
arm gcc tools
```

Mechanical:

```
openscad
kicad
```

## Old PowerShell Profile Notes

```
$env:Path += ";C:\Program Files (x86)\GNU Arm Embedded Toolchain\9 2020-q2-update\bin\"
$env:Path += "; C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.27.29110\bin\Hostx64\x64\"
$env:Path += "; C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.27.29110\bin\Hostx64\x86\"
$env:Path += "; C:\Program Files\fmpp\bin"
$env:Path += "; C:\Program Files\Java\jre1.8.0_261\bin"
$env:Path += "; C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
$env:Path += "; C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\SDK\ScopeCppSDK\vc15\SDK\bin"
```

## Old Universal Binary Notes

```
cmake ~/Research/ultralight-tags -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" -DCMAKE_OSX_DEPLOYMENT_TARGET="12.3"
```

Old libusb universal build note:

```
./configure CFLAGS="-arch x86_64 -arch arm64" CXXFLAGS="-arch x86_64 -arch arm64"
```

Old protobuf universal build note:

```
./configure CFLAGS="-arch x86_64 -arch arm64" CXXFLAGS="-arch x86_64 -arch arm64"
```

## Old Windows Qt Notes

Qt is provided by the installed Qt distribution for normal Windows builds. Use
the default preset and the `x64-windows` triplet for non-Qt dependencies instead
of building Qt with vcpkg or setting `Qt6_DIR`.

If you configure from a Visual Studio command prompt, open the x64 native tools
prompt for the installed Visual Studio version so the generator selects the
64-bit compiler environment.

```
cmake --preset default
cmake --build --preset default
cmake --install c:/software-build-external-qt --config Release
```

## Old Windows Static Build Notes

These are retained only for review. They describe the earlier static Windows
approach and are not the current build path.

Old vcpkg/static dependency notes:

```
vcpkg.exe install protobuf --triplet=x64-windows-static
vcpkg.exe install qt:x64-windows-static
vcpkg.exe install libusb:x64-windows-static
vcpkg.exe install ms-angle:x64-windows-static
```

Old extra setup notes:

```
Install nanopb binary under root_dir/nanopb.
Install pkg-config with: choco install pkgconfiglite -y
```

Old configure examples:

```
cmake -DVCPKG_TARGET_TRIPLET="x64-mingw-static" -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="d:/vcpkg/scripts/buildsystems/vcpkg.cmake" c:/Users/geobrown/ultralight-tags

cmake -DVCPKG_TARGET_TRIPLET="x64-windows-static" -DCMAKE_TOOLCHAIN_FILE="c:/users/geoff/vcpkg/scripts/buildsystems/vcpkg.cmake" c:/Users/geoff/ultralight-tags

qt-cmake -B Build -S c:\users\geobrown\ultralight-tags --preset default -G "Visual Studio 17 2022"
```

Old static Qt build note:

```
..\Qt\6.10.2\Src\configure.bat -prefix C:\QT-build\ -static -static-runtime -release -nomake examples -nomake tests --skip qtquick3dphysics --skip qtopcua
cmake --build .
cmake --install . --prefix c:\Qt6-static
```

Old static Qt configure note:

```
$env:Qt6_DIR = "C:\Qt6-static\lib\cmake\Qt6\"
cmake -DVCPKG_TARGET_TRIPLET="x64-windows-static" -DCMAKE_TOOLCHAIN_FILE="c:/users/geoff/vcpkg/scripts/buildsystems/vcpkg.cmake" -D Qt6_DIR="c:/Qt6-static/lib/cmake/Qt6" c:/Users/geoff/ultralight-tags
cmake --build . --config Release
cmake --install . --prefix install-directory-path
```

Old command-prompt note:

```
Use the x64 Native Tools Command Prompt for the installed Visual Studio version
so the compiler environment is 64-bit.
```
