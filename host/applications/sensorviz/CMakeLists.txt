cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure sensorviz")

if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(SENSORVIZ_SQLITE_INCLUDE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
  set(SENSORVIZ_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(SENSORVIZ_SQLITE_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
  set(SENSORVIZ_SQLITE_TARGET SQLite::SQLite3)
endif()
set(SENSORVIZ_SQLITE_LINK_TARGET ${SENSORVIZ_SQLITE_TARGET})

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
  sqlite_loader.cpp
  stream_actions.cpp
  transforms.cpp)

#[[
  sensorviz_core: the Qt-free half of sensorViz. View state (ViewModel),
  plot policy (buildPlotSpec) and the stream model, in standard C++ over the
  sensordata library, so a non-Qt front end can reuse them. It deliberately
  links no Qt: a Qt include here fails to compile.
#]]
add_library(sensorviz_core STATIC
  core/plot_spec.cpp
  core/stream_meta.cpp
  core/view_model.cpp)
target_include_directories(sensorviz_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_features(sensorviz_core PUBLIC cxx_std_17)
target_link_libraries(sensorviz_core PUBLIC sensordata)
set_target_properties(sensorviz_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

qt_add_executable(sensorviz MACOSX_BUNDLE WIN32 ${sensorviz_SRCS})
target_include_directories(sensorviz PRIVATE ${SENSORVIZ_SQLITE_INCLUDE_DIRS})
target_compile_definitions(sensorviz PRIVATE
  SENSORVIZ_DOCS_BUILD_DIR="${HOST_DOCS_BUILD_DIR}"
  SENSORVIZ_DOCS_SOURCE_DIR="${HOST_DOCS_SOURCE_DIR}")
set_target_properties(sensorviz
    PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
)

if (APPLE)
  set_target_properties(sensorviz PROPERTIES MACOSX_BUNDLE TRUE)
endif()

target_link_libraries(sensorviz PRIVATE ${SENSORVIZ_SQLITE_LINK_TARGET} host_common qcustomplot sensorviz_core sensordata sensorui Qt6::Widgets Qt6::Concurrent Qt6::PrintSupport)

# Offline checks of the Qt-free core: the plot policy, buildPlotSpec(),
# against the committed fixtures, and the ViewModel's state and preference
# handling. They link only sensorviz_core, so they also prove it builds
# without Qt. Plain main() programs with their own CHECK() macro, like the
# sensordata and tagcore checks, because this repository has no test harness.
# See test/README.md.
option(BUILD_SENSORVIZ_CHECKS
       "Build the offline sensorviz core checks (developer tool, run manually)"
       OFF)

if(BUILD_SENSORVIZ_CHECKS)
  foreach(check plotspec_check viewmodel_check)
    add_executable(${check} test/${check}.cc)
    target_link_libraries(${check} PRIVATE sensorviz_core)
    set_target_properties(${check} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_definitions(${check} PRIVATE
      TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    set_target_properties(${check} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  endforeach()
endif()
