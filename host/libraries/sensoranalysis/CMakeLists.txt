cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure sensoranalysis")

# The Qt-free core: compass calibration and the orientation solve on plain
# float vector and quaternion types (core/vecmath.h) that reproduce Qt's
# QVector3D and QQuaternion bit for bit. Command-line tools link this alone;
# the Qt classes below are adapters over it, so there is one implementation.
add_library(sensoranalysis_core STATIC core/compass_core.cpp)
target_include_directories(sensoranalysis_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_features(sensoranalysis_core PUBLIC cxx_std_17)

# The Qt-free reader for stored CalibrationConstants JSON, kept out of
# sensoranalysis_core so that the core needs no JSON library. It parses with
# protobuf's generic Struct, which keeps every number as the double parsed from
# the text, as Qt's reader does.
if(TARGET protobuf::libprotobuf)
  add_library(sensoranalysis_core_json STATIC core/calibration_json.cpp)
  target_link_libraries(sensoranalysis_core_json PUBLIC sensoranalysis_core protobuf::libprotobuf)
endif()

# Everything below is the Qt library and its checks. A host build without the
# Qt applications still gets the core above, which dataprocessing needs.
if(NOT BUILD_QT_APPS)
  return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(sensoranalysis STATIC
  accelcalibration.cpp
  compass_processor.cpp
  compass_types.cpp
  directions.cpp
  gravityfit.cpp
  magquality.cpp
  magretention.cpp)

target_include_directories(sensoranalysis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sensoranalysis PUBLIC sensoranalysis_core Qt6::Core Qt6::Gui)

# Offline assertion checks for the calibration quality metrics. Like the
# tagcore checks these are plain main() programs with assert(), not a
# registered test framework, because this repository has no test harness to
# register with. They need no tag and no capture: every case is synthetic data
# with a known answer. See test/README.md.
option(BUILD_SENSORANALYSIS_CHECKS
       "Build the offline sensoranalysis checks (developer tool, run manually)"
       OFF)

if(BUILD_SENSORANALYSIS_CHECKS)
  foreach(check accelcalibration_check gravityfit_check magquality_check
                magretention_check)
    add_executable(${check} test/${check}.cc)
    target_link_libraries(${check} PRIVATE sensoranalysis)
    set_target_properties(${check} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  endforeach()

  # Requires the Qt-free core to match a verbatim copy of the Qt code it
  # replaced, bit for bit, on the capture fixtures and synthetic cases. Links
  # Qt for the reference half only.
  add_executable(compass_core_parity_check test/compass_core_parity_check.cc)
  target_link_libraries(compass_core_parity_check
    PRIVATE sensoranalysis sensoranalysis_core_json proto Qt6::Core Qt6::Gui)
  target_compile_definitions(
    compass_core_parity_check PRIVATE TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  set_target_properties(compass_core_parity_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

  # A tool rather than a check: it takes a capture and prints what the
  # calibration code makes of it, and never decides anything is wrong. Built
  # behind the same option because it is developer-only.
  add_executable(capture_replay test/capture_replay.cc)
  target_link_libraries(capture_replay PRIVATE sensoranalysis)
  # Fixtures live in the source tree and the binary runs from the build tree,
  # so compile the source root in and let a repository-relative path resolve
  # against it. Same treatment, and the same reason, as qtcalibrate.
  target_compile_definitions(
    capture_replay PRIVATE TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  set_target_properties(capture_replay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
endif()
