
See [Documentation](https://geoffreymbrown.github.io/ultralight-tags/) to explore this project and for complete build instructions.


# CMake Build Options

The top-level CMake build is controlled with cache options passed at configure
time using `-DNAME=VALUE`. For example:

```
cmake -S . -B build-host -DBUILD_EMBEDDED=OFF
cmake -S . -B build-package -DBUILD_QT_APPS=ON -DMACOS_SIGN_APPS=ON
```

The main build-shape options are:

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_HOST` | `ON` | Build the host-side libraries, command-line tools, and host support code. |
| `BUILD_QT_APPS` | `ON` | Build the Qt GUI applications under `software/host`. This requires Qt to be available to CMake. |
| `BUILD_EMBEDDED` | `ON` | Build the embedded firmware targets. This requires the ARM toolchain, ChibiOS, nanopb, Java, `fmpp`, and `make`. |

Useful configure combinations:

```
# Host tools and Qt apps only
cmake -S . -B build-host -DBUILD_EMBEDDED=OFF

# Embedded firmware only
cmake -S . -B build-embedded -DBUILD_HOST=OFF -DBUILD_QT_APPS=OFF

# Everything
cmake -S . -B build-all
```

Dependency and platform cache variables:

| Variable | Default | Description |
| --- | --- | --- |
| `CMAKE_INSTALL_PREFIX` | build directory | Install and package staging location. The project defaults this to the active build directory. |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | `12.3` | Minimum macOS version used when building host software on macOS. |
| `Protobuf_USE_STATIC_LIBS` | `ON` | Prefer static Protobuf libraries when CMake searches for Protobuf. |
| `NANOPB_SRC_ROOT_FOLDER` | `$NANOPB_ROOT`, then `nanopb` | Root of the nanopb installation used by embedded builds. |
| `NANOPB_GENERATOR_SOURCE_DIR` | `${NANOPB_SRC_ROOT_FOLDER}/generator-bin` | Location of the nanopb generator scripts or packaged generator. |
| `CHIBIOS_DIR` | `$CHIBIOS_DIR`, then `ChibiOS` | ChibiOS source tree used by embedded firmware targets. |

On Windows, pass the vcpkg toolchain and triplet when configuring if CMake is
not being run through a preset:

```
cmake -S . -B build ^
  -DCMAKE_TOOLCHAIN_FILE=c:/users/geoff/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

macOS packaging and signing options:

| Option | Default | Description |
| --- | --- | --- |
| `MACOS_SIGN_APPS` | `ON` | Sign `.app` bundles during package staging. |
| `MACOS_CODE_SIGN_IDENTITY` | `Developer ID Application: Indiana University (5J69S77A7G)` | Signing identity passed to `codesign` or `macdeployqt -codesign`. |
| `MACOS_CODE_SIGN_ENTITLEMENTS` | empty | Optional entitlements plist used when signing app bundles. |
| `MACOS_USE_MACDEPLOYQT_DEPLOYMENT` | `ON` | Let `macdeployqt` handle Qt deployment and signing during package staging. |
| `MACOS_DEPLOY_APPSTORE_COMPLIANT` | `ON` | Pass `-appstore-compliant` to `macdeployqt`, which skips Qt plugins that are not App Store compliant. |
| `MACOS_MACDEPLOYQT_EXTRA_OPTIONS` | empty | Extra command-line options appended to each `macdeployqt` invocation. |
| `MACOS_DEPLOY_QT_PLUGINS_MANUALLY` | `ON` | Used by the fallback manual deploy path when `MACOS_USE_MACDEPLOYQT_DEPLOYMENT=OFF`. |
| `MACOS_KEEP_ONLY_QSQLITE_PLUGIN` | `ON` | Remove non-SQLite Qt SQL driver plugins before signing in the manual deploy path. |
| `MACOS_FIXUP_BUNDLE_DEPENDENCIES` | `ON` | Copy and rewrite non-system dylib dependencies into bundles in the manual deploy path. |
| `MACOS_BUNDLE_FIXUP_EXTRA_DIRS` | empty | Extra library directories searched by the manual bundle dependency fixup step. |

For normal macOS DMG packaging, use the default package flags and set the
signing identity if the default does not match the local keychain:

```
cmake -S . -B build-package \
  -DBUILD_QT_APPS=ON \
  -DMACOS_CODE_SIGN_IDENTITY="Indiana University (5J69S77A7G)"
```


# Build Prerequisites and Environment

Several builds assume external tools and libraries are available either on
`PATH`, through CMake package discovery, or through the cache variables listed
above.

Common requirements:

| Requirement | Used by | How CMake finds it |
| --- | --- | --- |
| CMake 3.20 or newer | all builds | Run directly as `cmake`. |
| C++ compiler with C++20 support | host tools, Qt apps, proto helpers | Selected by the CMake generator or toolchain file. |
| Protobuf | all builds | `find_package(Protobuf)`, with `Protobuf_USE_STATIC_LIBS=ON`. |
| Git | embedded version header generation | `find_package(Git)` in `cmake/version.cmake`. |

Host library and command-line tools:

| Requirement | Notes |
| --- | --- |
| `pkg-config` | Required by `software/host/lib` to locate `libusb-1.0`. |
| `libusb-1.0` | Required for the host `tag` library. If installed in a non-standard prefix, set `PKG_CONFIG_PATH` so `pkg-config` can find `libusb-1.0.pc`. |

Qt applications:

| Requirement | Notes |
| --- | --- |
| Qt 6 | Required when `BUILD_QT_APPS=ON`. The apps use Qt components including `Core`, `Gui`, `Widgets`, `PrintSupport`, `Sql`, `Svg`, `SvgWidgets`, `Qml`, `Quick`, and `QuickWidgets`. |
| `Qt6_DIR` or `CMAKE_PREFIX_PATH` | Set one of these if CMake cannot find Qt automatically. `Qt6_DIR` should point at the Qt `lib/cmake/Qt6` directory. |
| Qt `bin` directory on `PATH` | Needed for packaging tools such as `macdeployqt` and `windeployqt`. |

macOS packaging:

| Requirement | Notes |
| --- | --- |
| `macdeployqt` | Required when packaging Qt apps on macOS. It is usually found from the selected Qt installation. |
| `codesign` | Required when `MACOS_SIGN_APPS=ON`. The signing identity must be available in the local keychain. |
| `install_name_tool` | Used by the fallback bundle fixup path when `MACOS_USE_MACDEPLOYQT_DEPLOYMENT=OFF`. |

Windows packaging:

| Requirement | Notes |
| --- | --- |
| `windeployqt` | Required when packaging Qt apps on Windows. It is usually found from the selected Qt installation. |
| vcpkg toolchain | Recommended for Protobuf, Qt, and libusb on Windows. Pass `CMAKE_TOOLCHAIN_FILE` and `VCPKG_TARGET_TRIPLET` at configure time. |
| Static Qt location | The current Windows host CMake file assumes `c:/Qt6_static/lib/cmake/Qt6` for Qt unless the build is adjusted. |

Embedded firmware:

| Requirement | Notes |
| --- | --- |
| `arm-none-eabi-gcc` | Required when `BUILD_EMBEDDED=ON`; must be on `PATH`. |
| `make` | Required for the ChibiOS-based firmware builds; must be on `PATH`. |
| `fmpp` | Required for board file generation; must be on `PATH`. On Windows, CMake also searches `c:/Program Files/fmpp/bin`. |
| Java runtime | Required by `fmpp`. Make sure `java` is available on `PATH`. |
| ChibiOS | Set `CHIBIOS_DIR`, or place the ChibiOS tree at `ChibiOS` in the repository root. |
| nanopb | Set `NANOPB_ROOT`, or place nanopb at `nanopb` in the repository root. Embedded proto generation expects the generator under `${NANOPB_SRC_ROOT_FOLDER}/generator-bin`. |
| STM32CubeProgrammer | Optional for building, required for generated download/DFU targets. CMake looks for `STM32_Programmer_CLI` on `PATH` and in common install locations. |

Useful environment variables:

```
CHIBIOS_DIR=/path/to/ChibiOS
NANOPB_ROOT=/path/to/nanopb
PKG_CONFIG_PATH=/path/to/libusb/lib/pkgconfig:$PKG_CONFIG_PATH
Qt6_DIR=/path/to/Qt/6.x/platform/lib/cmake/Qt6
PATH=/path/to/Qt/6.x/platform/bin:/path/to/gcc-arm/bin:/path/to/fmpp/bin:$PATH
```


# Building and Packaging on macOS

Make sure the Qt `bin` directory for the version being used is on your
`PATH`. CMake needs to find tools such as `macdeployqt`.

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

The macOS package flow runs `macdeployqt` during package staging, then CPack
creates the DragNDrop DMG. These package-related options default to `ON`:

```
MACOS_SIGN_APPS
MACOS_USE_MACDEPLOYQT_DEPLOYMENT
MACOS_DEPLOY_APPSTORE_COMPLIANT
MACOS_KEEP_ONLY_QSQLITE_PLUGIN
MACOS_DEPLOY_QT_PLUGINS_MANUALLY
MACOS_FIXUP_BUNDLE_DEPENDENCIES
```

With the defaults, the package step runs `macdeployqt` with the configured
signing identity and `-appstore-compliant`. A few warnings about skipped
non-app-store-compliant SQL plugins, or missing optional Mimer client
libraries, can appear during packaging. They are expected as long as the apps
start and only the SQLite Qt SQL driver is needed.

To verify a packaged app:

```
codesign --verify --deep --verbose path/to/App.app
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


No longer building qt -- using distributed version
-------------

Building QT

https://doc.qt.io/qt-6/macos-building.html


_ ./configure -- -DCMAKE_OSX_ARCHITECTURES="x86_64h;arm64"

Install	macports

sudo port install libusb-1.0
#sudo port install qt6




# building with MSVC

you have my deepest sympathy

## For building host software    

* install vcpkg -- within vcpkg:
  * install protobuf --- vcpkg.exe install protobuf --triplet=x64-windows-static
  * install qt:x64-windows-static
  * install libusb:x64-windows-static
  * install ms-angle:x64-windows-static
* install nanopb binary  -- unpack in root_dir/nanopb ;   using the binary version is more reliable than using the source version
* install pkg-config choco install pkgconfiglite -y

**Note** cmake now understands vcpkg so the direct installation within vcpkg is no longer needed.   Instead there are json files in the ultralight-tags directory that cmake uses in install vcpkg files.

### Configure main build

cmake -DVCPKG_TARGET_TRIPLET="x64-mingw-static" -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="d:/vcpkg/scripts/buildsystems/vcpkg.cmake" c:/Users/geobrown/ultralight-tags

**Note** cmake has trouble on windows finding Qt.  The workaround is to use qt-cmake from the qt installation.

```
  cmake -DVCPKG_TARGET_TRIPLET="x64-windows-static" -DCMAKE_TOOLCHAIN_FILE="c:/users/geoff/vcpkg/scripts/buildsystems/vcpkg.cmake" c:/Users/geoff/ultralight-tags
```

```
qt-cmake -B Build -S c:\users\geobrown\ultralight-tags --preset default -G "Visual Studio 17 2022"
```

### Build release/debug

```
cmake --build . --config Debug  
cmake --build . --config Release
```

## Building Embedded Software

* install ChibiOS (submodule in repo)
* install gnu-arm toolchain
* install java
* install fmpp
* install make (choco)

### Building Tags

```
cmake --build . --target [BitTagv6|BitTagv5|BitTagNucleo]
cmake --build . --target BitTagv6-download

```

### Building Base boards

```
cmake --build . --target bittag-base-jlcpcb-v3
cmake --build . --target bittag-base-jlcpcb-v3-dfu
cmake --build . --target tag-breakout-base-jlcpcb32-v1
cmake --build . --target tag-breakout-base-jlcpcb32-v1-dfu
```

 The -dfu rule attempts to  program the board.

## Programming Embedded Hardware

* Install [stm32cube programmer](https://wiki.st.com/stm32mpu/wiki/STM32CubeProgrammer)

## Programming with STM32Cube Programmer

```
~/Software/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe -c port=SWD mode=UR -d ch.elf -v -g 0x08000000 
```

* `port=SWD` driver
* `mode=UR` -- attach under reset
* `-g 0x08000000`  -- execute program after completion
* `-v` verify
* `-vb 3`  verbose logging (for debugging)


### DFU for base boards

Make sure a device is in dfu mode

```
STM32_Programmer_CLI.exe -l usb
```

To program from command line

```
 ~/Software/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=usb1 -d build/ch.elf 
 ```

## Installing protobuf on linux

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

# Tools

## Host and Embedded 

cmake
protobuf -- protoc, libprotobuf, python (pip install protobuf)
nanopb
python


## Host only

Qt5
libusb
vcpkg (windows)

## Embedded only

chibios
fmpp
stm32 programmer cli
arm gcc tools

## Mechanical

openscad 
kicad


## Powershell $Profile

$env:Path += ";C:\Program Files (x86)\GNU Arm Embedded Toolchain\9 2020-q2-update\bin\"
$env:Path += "; C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.27.29110\bin\Hostx64\x64\"
$env:Path += "; C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.27.29110\bin\Hostx64\x86\"
$env:Path += "; C:\Program Files\fmpp\bin"
$env:Path += "; C:\Program Files\Java\jre1.8.0_261\bin"
$env:Path += "; C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
$env:Path += "; C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\SDK\ScopeCppSDK\vc15\SDK\bin"


# Notes on universal binary

cmake ~/Research/ultralight-tags    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" -DCMAKE_OSX_DEPLOYMENT_TARGET="12.3"

libusb

 ./configure CFLAGS="-arch x86_64 -arch arm64" CXXFLAGS="-arch x86_64 -arch arm64"

 protobuf

 ./configure CFLAGS="-arch x86_64 -arch arm64" CXXFLAGS="-arch x86_64 -arch arm64"


 # Notes on Building Static qt for windows

 - Download sources
 - use ""x64 Native Tools Command Prompt for VS 2022" for building !!  This is important because otherwise the powershell window will build a 32bit binary
 - 
 - ..\Qt\6.10.2\Src\configure.bat -prefix C:\QT-build\ -static -static-runtime -release -nomake examples -nomake tests --skip qtquick3dphysics --skip qtopcua
 - cmake --build .
 - cmake --install . --prefix c:\Qt6-static


 Fucking Windows 11 opens 32 bit developer powershell by default!  Have to find the 64 bit version in visual studio tools-commandline


 Open the Start Menu and search for the "x64 Native Tools Command Prompt" for your installed version of Visual Studio (e.g., "x64 Native Tools Command Prompt for VS 2022"). This ensures your environment variables are correctly set for a 64-bit build.

 Building for windows 2/1/26

- $env:Qt6_DIR = "C:\Qt6-static\lib\cmake\Qt6\"  
- cmake -DVCPKG_TARGET_TRIPLET="x64-windows-static" -DCMAKE_TOOLCHAIN_FILE="c:/users/geoff/vcpkg/scripts/buildsystems/vcpkg.cmake" -D Qt6_DIR="c:/Qt6-static/lib/cmake/Qt6" c:/Users/geoff/ultralight-tags

- cmake --Build . --config Release  
- cmake --install . --prefix install-directory-path
