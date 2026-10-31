cmake_minimum_required(VERSION 3.15)

# Not on Linux, whose installed tree is relocatable; see host/CMakeLists.txt.
if(APPLE OR WIN32)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

#[[
  @brief  Adds one tagcore command line tool.

  @details Links the tool against tagcore and the monitor interface and puts
           it in <build>/<Config>/bin, or in bin/internal with INTERNAL: the
           developer and bench tools go there, so that bin/ holds only what
           the package installs (plus the test and check programs other
           directories build). On macOS it is a plain executable, not an app
           bundle: its dependencies are linked statically, so there is nothing
           for a bundle to carry, and a plain executable runs from a terminal
           by its name. The package installs the shipped tools to
           tag_tools/bin; INTERNAL tools are never installed (the install list
           is host_cli_install_targets in host/CMakeLists.txt).

  @param  name      Target and executable name.
  @param  INTERNAL  A developer or bench tool: built into bin/internal.
  @param  ARGN      Sources, optionally preceded by add_executable() keywords.
                    WIN32 is overridden: the tool is linked /SUBSYSTEM:CONSOLE.

  Example:
    host_cli_tool(tag-info WIN32 tag-info.cc options.cc)
    host_cli_tool(tag-peek INTERNAL WIN32 tag-peek.cc options.cc)
#]]
function(host_cli_tool name)
  cmake_parse_arguments(ARG "INTERNAL" "" "" ${ARGN})
  set(_bin_dir "${CMAKE_BINARY_DIR}/$<CONFIG>/bin")
  if(ARG_INTERNAL)
    set(_bin_dir "${_bin_dir}/internal")
  endif()

  add_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
  if(WIN32)
    target_link_libraries(${name} tagcore tag_monitor_interface)
  else()
    target_link_libraries(${name} tagcore pthread tag_monitor_interface)
  endif()


  set_target_properties(
    ${name}
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
               RUNTIME_OUTPUT_DIRECTORY "${_bin_dir}")
  if(WIN32)
    set_target_properties(${name} PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)
  endif()
endfunction()


# The shipped tools (host_cli_install_targets in host/CMakeLists.txt) build
# into bin/; the developer and bench tools, marked INTERNAL, into
# bin/internal. host/commandline/README.md says which is which and why.
host_cli_tool(tag-dwnld WIN32 dwnld.cc options.cc)
host_cli_tool(tag-test WIN32 test.cc options.cc)
host_cli_tool(tag-test-example INTERNAL WIN32 test-example.cc options.cc)
host_cli_tool(tag-info WIN32 tag-info.cc options.cc)
host_cli_tool(tag-reset WIN32 tag-reset.cc options.cc)
host_cli_tool(tag-cal-write INTERNAL WIN32 tag-cal-write.cc options.cc)
host_cli_tool(tag-peek INTERNAL WIN32 tag-peek.cc options.cc)
host_cli_tool(tag-side-channel-check INTERNAL WIN32 tag-side-channel-check.cc options.cc)
host_cli_tool(tag-start WIN32 tag-start.cc options.cc)
host_cli_tool(tag-stop WIN32 tag-stop.cc options.cc)
host_cli_tool(tag-cal WIN32 tag-calibrate.cc options.cc)
host_cli_tool(tag-monitor-test INTERNAL WIN32 tag-monitor-test.cc options.cc)
host_cli_tool(tag-attach-cycle INTERNAL WIN32 tag-attach-cycle.cc options.cc)
host_cli_tool(tag-capture INTERNAL WIN32 tag-capture.cc)
host_cli_tool(tag-xflash INTERNAL WIN32 tag-xflash.cc)
host_cli_tool(tag-sramcall INTERNAL WIN32 tag-sramcall.cc)
host_cli_tool(tag-rebuild INTERNAL WIN32 tag-rebuild.cc)
host_cli_tool(tag-log-import INTERNAL WIN32 tag-log-import.cc)
host_cli_tool(tag-txt-import WIN32 tag-txt-import.cc)
host_cli_tool(qtmonitor-fixture-capture INTERNAL WIN32 qtmonitor-fixture-capture.cc)

# dataprocessing does not use Qt. Its compass math is the Qt-free sensoranalysis
# core, bit-identical to the Qt classes the applications use, and its calibration
# reader parses with protobuf. It is a plain executable like the tools above, but
# it is not built with host_cli_tool(): it needs SQLite and the core, not tagcore
# or libusb. It borrows two self-contained files from tagcore -- the vendored
# cxxopts header and the SHA-256 it hashes the input with -- by path.
if(TARGET sensoranalysis_core_json)
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    set(DATAPROCESSING_SQLITE_INCLUDE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    set(DATAPROCESSING_SQLITE_TARGET unofficial::sqlite3::sqlite3)
  else()
    find_package(SQLite3 REQUIRED)
    set(DATAPROCESSING_SQLITE_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
    set(DATAPROCESSING_SQLITE_TARGET SQLite::SQLite3)
  endif()

  set(TAGCORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libraries/tagcore")
  add_executable(dataprocessing dataprocessing.cc "${TAGCORE_SOURCE_DIR}/recovery/sha256.cc")
  target_compile_features(dataprocessing PRIVATE cxx_std_20)
  target_include_directories(dataprocessing PRIVATE ${DATAPROCESSING_SQLITE_INCLUDE_DIRS}
                                                    "${TAGCORE_SOURCE_DIR}")
  # tag_monitor_interface supplies include/imutag_log_format.h, the superframe
  # size the IMUTag compass processor pairs samples by.
  target_link_libraries(dataprocessing PRIVATE sensoranalysis_core_json tag_monitor_interface
                                               ${DATAPROCESSING_SQLITE_TARGET})
  set_target_properties(
    dataprocessing
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
               RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
endif()
