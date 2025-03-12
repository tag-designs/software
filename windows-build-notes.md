cmake -B build -S  c:/users/geobrown/ultralight-tags --preset default
cmake --build . --config Release



qt-cmake -G "MinGW Makefiles" -DVCPKG_TARGET_TRIPLET="x64-mingw-static" -DCMAKE_TOOLCHAIN_FILE="d:/vcpkg/scripts/buildsystems/vcpkg.cmake"  c:/users/geobrown/ultralight-tags

Protobuf_DIR=d:/vcpkg/installed/x64-mingw-static/tools/protobuf
mac:



Note: compiling embedded software on windows is broken because of the use of "cat" in chibios make files

Install -
* chocolaty [must run in admin powershell]
    * choco install make
    * choco install pkgconfiglites

* Environment Variables
    * NANOPB_ROOT C:\users\geobrown\nanopb
    * VCPKG_ROOT  D:\vcpkg  --- skip, use builtin vcpkg
    * VCPKG_DEFAULT_TRIPLET x64-windows-static
    * STM32_PRG_PATH  C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin
    * CHIBIOS_DIR D:\ChibiOS

* Path additions
    * D:\vcpkg
    * C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin
    * c:\users\geobrown\fmpp\bin
    * D:\Qt\x64-static\bin
    * C:\ProgramData\chocolatey\bin


--- To build ultralight tools on windows --

see [qt path bug](https://bugreports.qt.io/browse/QTBUG-97615)

cmake -G "Visual Studio 17 2022" -Ax64 -DVCPKG_TARGET_TRIPLET="x64-windows-static" --preset=default -DQt6_DIR="d:/Qt/x64-static" -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="d:/Qt/x64-static"

Build -- 
* QT x64-windows-static


links - [Install with cmake](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell
)

```
cd D:\qt-build
cmd /E:ON /V:ON /k C:\Users\geobrown\qtvars.cmd
```

```
C:\Qt\6.8.2\src\configure.bat -static -debug-and-release -opensource -static-runtime -skip qt3d -skip qtactiveqt -skip qtandroidextras -skip qtcharts -skip qtconnectivity -skip qtdatavis3d -skip qtdeclarative -skip qtdoc -skip qtgamepad -skip qtlocation -skip qtlottie -skip qtmacextras -skip qtmultimedia -skip qtnetworkauth -skip qtpurchasing -skip qtquick3d -skip qtquickcontrols -skip qtquickcontrols2 -skip qtquicktimeline -skip qtremoteobjects -skip qtscript -skip qtsensors -skip qtspeech -skip qtsvg -skip qtwayland -skip qtwebglplugin -skip qtwebview -skip webengine -nomake examples -nomake tests -skip qtgraphs -skip qtmqtt -skip qtopcua -skip qt3d -skip qtquick3dphysics -skip qtquickeffectmaker -skip qtvirtualkeyboard -skip qtwebchannel -skip qtwebsockets -skip qtcoap -skip qtgrpc -skip qthttpserver -prefix d:/Qt/x64-static

```

```
in the build directory do this:

D:\Qt\src\configure.bat -static -debug-and-release -opensource -static-runtime -platform win32-msvc -nomake examples -nomake tests -prefix d:/Qt/x64-static
cmake --build .
cmake -DCMAKE_INSTALL_PREFIX=d:/Qt/x64-static install .


or

D:\Qt\src\configure.bat -static -debug-and-release -opensource -static-runtime -platform win32-g++ -nomake examples -nomake tests -prefix d:/Qt/x64-mingw-static
cmake --build .
cmake -DCMAKE_INSTALL_PREFIX=d:/Qt/x64-static install .
```