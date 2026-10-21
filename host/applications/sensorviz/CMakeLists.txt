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
  plot_spec.cpp
  plot_view.cpp
  plotting.cpp
  sensor_preferences.cpp
  sqlite_loader.cpp
  stream_actions.cpp
  stream_display_defaults.cpp
  transforms.cpp)

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

target_link_libraries(sensorviz PRIVATE ${SENSORVIZ_SQLITE_LINK_TARGET} host_common qcustomplot sensordata sensorui Qt6::Widgets Qt6::Concurrent Qt6::PrintSupport)

# Offline check of the plot policy, buildPlotSpec(), against the committed
# fixtures: no window, no QCustomPlot. A plain main() with its own CHECK()
# macro, like the sensordata and tagcore checks, because this repository has
# no test harness. See test/README.md.
option(BUILD_SENSORVIZ_CHECKS
       "Build the offline sensorviz plot-policy check (developer tool, run manually)"
       OFF)

if(BUILD_SENSORVIZ_CHECKS)
  add_executable(plotspec_check
    test/plotspec_check.cc
    plot_spec.cpp
    sqlite_loader.cpp
    stream_display_defaults.cpp)
  target_include_directories(plotspec_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" ${SENSORVIZ_SQLITE_INCLUDE_DIRS})
  target_link_libraries(plotspec_check PRIVATE
    ${SENSORVIZ_SQLITE_LINK_TARGET} sensordata sensoranalysis Qt6::Core Qt6::Gui)
  target_compile_definitions(plotspec_check PRIVATE
    TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  set_target_properties(plotspec_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
endif()
