#[[
  sensorviz_core: the Qt-free half of sensorViz. View state (ViewModel),
  plot policy (buildPlotSpec), the stream model, the log catalog (openLog),
  the preference file (PreferenceStore) and the display transforms
  (TransformRegistry), in standard C++ over sensordata, sqlite3 and
  sensoranalysis's core, so a non-Qt front end can reuse them.

  It is added by host/CMakeLists.txt with the other Qt-free libraries, not by
  the sensorviz application, so it builds with BUILD_HOST alone; the Qt
  application and the C interface both link it. It links no Qt: a Qt include
  here fails to compile, and the checks below, which link only this library,
  build in a host build without BUILD_QT_APPS.

  sqlite3 comes from vcpkg's unofficial-sqlite3 package when building with
  vcpkg and from the system otherwise, as for sensordata.
#]]
cmake_minimum_required(VERSION 3.15)

message("Configure sensorviz_core")

if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(SENSORVIZ_SQLITE_INCLUDE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
  set(SENSORVIZ_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(SENSORVIZ_SQLITE_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
  set(SENSORVIZ_SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(sensorviz_core STATIC
  plot_spec.cpp
  preference_store.cpp
  session.cpp
  stream_catalog.cpp
  stream_meta.cpp
  transforms.cpp
  view_model.cpp)
target_include_directories(sensorviz_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(sensorviz_core PUBLIC cxx_std_17)
target_link_libraries(sensorviz_core PUBLIC sensordata sensoranalysis_core_json)
target_link_libraries(sensorviz_core PRIVATE ${SENSORVIZ_SQLITE_TARGET})
target_include_directories(sensorviz_core PRIVATE ${SENSORVIZ_SQLITE_INCLUDE_DIRS})

#[[
  Offline checks of the core: the log catalog (openLog), the plot policy
  (buildPlotSpec) against the committed fixtures, the preference file, the
  display transforms, and the ViewModel's state and preference handling.
  Plain main() programs with their own CHECK() macro, like the sensordata
  and tagcore checks, because this repository has no test harness. Their
  sources are in ../test; see ../test/README.md.
#]]
option(BUILD_SENSORVIZ_CHECKS
       "Build the offline sensorviz core checks (developer tool, run manually)"
       OFF)

if(BUILD_SENSORVIZ_CHECKS)
  foreach(check catalog_check plotspec_check preference_check transforms_check viewmodel_check)
    add_executable(${check} ../test/${check}.cc)
    target_link_libraries(${check} PRIVATE sensorviz_core)
    target_compile_definitions(${check} PRIVATE
      TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    set_target_properties(${check} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  endforeach()
  # catalog_check edits a copy of a fixture to reach paths no fixture does.
  target_link_libraries(catalog_check PRIVATE ${SENSORVIZ_SQLITE_TARGET})
  target_include_directories(catalog_check PRIVATE ${SENSORVIZ_SQLITE_INCLUDE_DIRS})
endif()
