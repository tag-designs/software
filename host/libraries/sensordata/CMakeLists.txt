#[[
  sensordata: Qt-free, decimating read access to tagcore SQLite sensor logs.

  Loads a log's streams into a columnar layout (shared double time axes,
  float values, min/max pyramids) and answers M4 view queries, exact
  value read-back and full-precision row passes. Depends only on sqlite3,
  so it builds with BUILD_HOST alone, without BUILD_QT_APPS. See README.md.
#]]
cmake_minimum_required(VERSION 3.15)

message("Configure sensordata")

if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(SENSORDATA_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(SENSORDATA_SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(sensordata STATIC sensordata.cc minmax_pyramid.cc)
target_include_directories(sensordata PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(sensordata PUBLIC cxx_std_17)
target_link_libraries(sensordata PRIVATE ${SENSORDATA_SQLITE_TARGET})

# Offline assertion checks: view() against brute-force M4, exact read-back,
# the out-of-order fallback, derived streams, and every committed sensorviz
# fixture. A plain main() with assert(), like the tagcore and sensoranalysis
# checks, because this repository has no test harness. See test/README.md.
option(BUILD_SENSORDATA_CHECKS
       "Build the offline sensordata checks (developer tool, run manually)"
       OFF)

if(BUILD_SENSORDATA_CHECKS)
  add_executable(sensordata_check test/sensordata_check.cc)
  target_link_libraries(sensordata_check PRIVATE sensordata ${SENSORDATA_SQLITE_TARGET})
  # Fixtures live in the source tree and the binary runs from the build tree.
  target_compile_definitions(
    sensordata_check PRIVATE TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  set_target_properties(sensordata_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
endif()
