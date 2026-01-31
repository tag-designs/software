
See [Documentation](https://geoffreymbrown.github.io/ultralight-tags/) to explore this project and for complete build instructions.


# building with OS X


cmake  -DQt6_DIR=/Users/geobrown/Qt/6.5.3/macos  ~/Research/ultralight-tags 

Make sure to add Qt-version/bin (or current version) to your path so that the various find_program tools work (esp macdeployqt)

(use your signing authority)

macdeployqt bin/btviz.app -codesign="Indiana University"
macdeployqt bin/qtmonitor.app -codesign="Indiana University (5J69S77A7G)"
macdeployqt bin/tag-test.app -codesign="Indiana University"
        
To verify

codesign --verify --deep --verbose xyz.app

Then build the dmg file

cpack -G DragNDrop


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
 
