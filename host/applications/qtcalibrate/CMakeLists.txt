cmake_minimum_required(VERSION 3.15)

message("Configure qtcalibrate")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Instruct CMake to run moc automatically when needed

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt6 COMPONENTS Core Widgets Qml Quick QuickWidgets Svg CONFIG)
if(NOT Qt6_FOUND)
  message("Qt6 Not found")
  message("Cannot Build qtcalibrate")
  return()
endif()

set(qtcalibrate_SRCS
    compassdata.cpp
    # Inherited C magnetometer calibration solver and support routines.
    magcal/magcal.c
    magcal/matrix.c
    magcal/quality.c
    magplot.cpp
    main.cpp
    mainwindow.cpp
    sinbin.cpp)

qt6_wrap_ui(UI_HEADERS 
            mainwindow.ui)

qt_add_executable(
  qtcalibrate MACOSX_BUNDLE WIN32 ${qtcalibrate_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)

target_include_directories(qtcalibrate PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# qtcalibrate embeds the shared sensorui QML compass and 3D attitude widgets.
set_property(
  TARGET qtcalibrate
  PROPERTY QT_DEPLOY_QML_DIRS "${PROJECT_SOURCE_DIR}/host/libraries/sensorui/orientation_frame")

set_target_properties(
  qtcalibrate
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

if(APPLE)
  set_target_properties(qtcalibrate PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
  # windeployqt(qtprogram)
endif()

target_link_libraries(qtcalibrate PRIVATE tagcore host_common sensorui Qt6::Core Qt6::Widgets Qt6::QuickWidgets)
