Readme file for NanoTag repo

# building with OS X

Use the "brew" version of QT because it links statically.

Make sure to add /usr/local/Cellar/qt/5.15.1/bin (or current version) to your path so that the various find_program tools work (esp macdeployqt)

Building the dmg file

cpack -G DragNDrop


# building with MSVC

you have my deepest sympathy

## For building host software    

* install vcpkg -- within vcpkg:
  * install protobuf:x64-window-static
  * install qt5:x64-windows-static
  * install libusb:x64-windows-static
  * install ms-angle:x64-windows-static
* install nanopb (submodule in ropo)

### Configure main build

```
cmake -G "Visual Studio 16 2019" -Ax64 -DVCPKG_TARGET_TRIPLET="x64-windows-static" -DCMAKE_TOOLCHAIN_FILE="c:/users/geobrown/vcpkg/scripts/buildsystems/vcpkg.cmake" ..
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
cmake --build . --target bittag-base-v5
cmake --build . --target bittag-base-v5-dfu
cmake --build . --target bittag-base-jlcpcb-v2
cmake --build . --target bittag-base-jlcpcb-v2-dfu
```

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

 ## vcpkg linux

sudo apt-get install rfbproxy
sudo apt-get install libudev-dev
sudo  apt-get install libx11-dev
sudo apt-get install libxi-dev libgl1-mesa-dev libglu1-mesa-dev mesa-common-dev libxrandr-dev libxxf86vm-dev
sudo apt-get install libxcb-xinerama0-dev 

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