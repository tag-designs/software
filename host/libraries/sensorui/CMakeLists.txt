cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure sensorui")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Qml Quick QuickWidgets Svg)

set(sensorui_SRCS
  compass_calibration_dialog.cpp
  compass_calibration_dialog.ui
  compass_display.cpp
  sensorui_resources.cpp)

qt_add_resources(sensorui_SRCS sensorui.qrc)

add_library(sensorui STATIC ${sensorui_SRCS})

target_include_directories(sensorui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  sensorui
  PUBLIC
    sensoranalysis
    Qt6::Widgets
    Qt6::QuickWidgets
  PRIVATE
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg)

set_property(
  TARGET sensorui
  PROPERTY QT_DEPLOY_QML_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/orientation_frame")
