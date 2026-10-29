#[[
  sensorviz: the Qt application for viewing the SQLite logs that tagcore
  writes, plotting their streams with qcustomplot and showing compass and
  attitude views from sensorui.

  This is the Qt front end. Its Qt-free half, sensorviz_core (the log
  reader, view state, plot policy and preference file), is built from
  core/CMakeLists.txt, which host/CMakeLists.txt adds without Qt; the core's
  checks are built there too. Qt6 Core, Widgets, Concurrent, PrintSupport,
  Qml, Quick, QuickWidgets and Svg are REQUIRED, so a missing module fails
  the configure. The user guide path and the documentation-screenshot
  default directory come from host_common's compile definitions.
#]]
cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure sensorviz")

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent PrintSupport Qml Quick QuickWidgets Svg)

if (NOT Qt6_FOUND)
  message("Qt6 not found")
  message("Cannot build sensorviz")
  return()
endif()

set(sensorviz_SRCS
  compass_transforms.cpp
  controls.cpp
  dataloading.cpp
  documentation_capture.cpp
  interaction.cpp
  main.cpp
  mainwindow.cpp
  plot_view.cpp
  plotting.cpp
  sensor_preferences.cpp
  state_dump.cpp
  stream_actions.cpp
  transforms.cpp)

qt_add_executable(sensorviz MACOSX_BUNDLE WIN32 ${sensorviz_SRCS})
set_target_properties(sensorviz
    PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
)

if (APPLE)
  set_target_properties(sensorviz PROPERTIES MACOSX_BUNDLE TRUE)
endif()

target_link_libraries(sensorviz PRIVATE host_common qcustomplot sensorviz_core sensordata sensorui Qt6::Widgets Qt6::Concurrent Qt6::PrintSupport)
