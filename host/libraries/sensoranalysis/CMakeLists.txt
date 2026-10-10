cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure sensoranalysis")

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(sensoranalysis STATIC
  accelcalibration.cpp
  compass_processor.cpp
  compass_types.cpp
  directions.cpp
  posecheck.cpp
  gravityfit.cpp
  magquality.cpp
  magretention.cpp)

target_include_directories(sensoranalysis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sensoranalysis PUBLIC Qt6::Core Qt6::Gui)

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
                magretention_check posecheck_check)
    add_executable(${check} test/${check}.cc)
    target_link_libraries(${check} PRIVATE sensoranalysis)
    set_target_properties(${check} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  endforeach()

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
