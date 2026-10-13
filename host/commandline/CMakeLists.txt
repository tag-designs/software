cmake_minimum_required(VERSION 3.15)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

#[[
  @brief  Links <bin>/<name> to the executable inside a macOS tool bundle.

  @details Only dataprocessing needs this. It links Qt, and macdeployqt can
           deploy Qt only into a bundle, so on macOS it is the one command
           line tool still built as an app, with its executable at
           <name>.app/Contents/MacOS/<name>. After each build this places a
           relative symlink beside the bundle, so it runs as <bin>/<name> like
           every other tool. The link lives only in the build tree; the
           package gets its own, made by host/CMakeLists.txt. Does nothing off
           macOS.

  @param  name  An executable target built with MACOSX_BUNDLE.

  Example:
    host_cli_bundle_link(dataprocessing)
#]]
function(host_cli_bundle_link name)
  if(APPLE)
    add_custom_command(
      TARGET ${name}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_BUNDLE_DIR:${name}>/../$<TARGET_FILE_NAME:${name}>"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "$<TARGET_FILE_NAME:${name}>.app/Contents/MacOS/$<TARGET_FILE_NAME:${name}>"
              "$<TARGET_BUNDLE_DIR:${name}>/../$<TARGET_FILE_NAME:${name}>"
      VERBATIM)
  endif()
endfunction()

#[[
  @brief  Adds one tagcore command line tool.

  @details Links the tool against tagcore and the monitor interface and puts
           it in <bin>. On macOS it is a plain executable, not an app bundle:
           its dependencies are linked statically, so there is nothing for a
           bundle to carry, and a plain executable runs from a terminal by its
           name. The package installs it to tag_tools/bin.

  @param  name  Target and executable name.
  @param  ARGN  Sources, optionally preceded by add_executable() keywords.
                WIN32 is overridden: the tool is linked /SUBSYSTEM:CONSOLE.

  Example:
    host_cli_tool(tag-info WIN32 tag-info.cc options.cc)
#]]
function(host_cli_tool name)

  add_executable(${name} ${ARGN})
  if(WIN32)
    target_link_libraries(${name} tagcore tag_monitor_interface)
  else()
    target_link_libraries(${name} tagcore pthread tag_monitor_interface)
  endif()


  set_target_properties(
    ${name}
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
               RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  if(WIN32)
    set_target_properties(${name} PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)
  endif()
endfunction()


host_cli_tool(tag-dwnld WIN32 dwnld.cc options.cc)
host_cli_tool(tag-test WIN32 test.cc options.cc)
host_cli_tool(tag-test-example WIN32 test-example.cc options.cc)
host_cli_tool(tag-info WIN32 tag-info.cc options.cc)
host_cli_tool(tag-reset WIN32 tag-reset.cc options.cc)
host_cli_tool(tag-cal-write WIN32 tag-cal-write.cc options.cc)
host_cli_tool(tag-peek WIN32 tag-peek.cc options.cc)
host_cli_tool(tag-start WIN32 tag-start.cc options.cc)
host_cli_tool(tag-stop WIN32 tag-stop.cc options.cc)
host_cli_tool(tag-cal WIN32 tag-calibrate.cc options.cc)
host_cli_tool(tag-monitor-test WIN32 tag-monitor-test.cc options.cc)
host_cli_tool(tag-attach-cycle WIN32 tag-attach-cycle.cc options.cc)
host_cli_tool(tag-capture WIN32 tag-capture.cc)
host_cli_tool(tag-xflash WIN32 tag-xflash.cc)
host_cli_tool(tag-sramcall WIN32 tag-sramcall.cc)
host_cli_tool(tag-rebuild WIN32 tag-rebuild.cc)
host_cli_tool(qtmonitor-fixture-capture WIN32 qtmonitor-fixture-capture.cc)

if(TARGET sensoranalysis)
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    set(DATAPROCESSING_SQLITE_INCLUDE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    set(DATAPROCESSING_SQLITE_TARGET unofficial::sqlite3::sqlite3)
  else()
    find_package(SQLite3 REQUIRED)
    set(DATAPROCESSING_SQLITE_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
    set(DATAPROCESSING_SQLITE_TARGET SQLite::SQLite3)
  endif()

  add_executable(dataprocessing MACOSX_BUNDLE WIN32 dataprocessing.cc)
  target_compile_features(dataprocessing PRIVATE cxx_std_20)
  target_include_directories(dataprocessing PRIVATE ${DATAPROCESSING_SQLITE_INCLUDE_DIRS})
  # tag_monitor_interface supplies include/imutag_log_format.h, the superframe
  # size the IMUTag compass processor pairs samples by.
  target_link_libraries(dataprocessing PRIVATE sensoranalysis tag_monitor_interface
                                               ${DATAPROCESSING_SQLITE_TARGET})
  set_target_properties(
    dataprocessing
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
               RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  if(WIN32)
    set_target_properties(dataprocessing PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)
  endif()
  host_cli_bundle_link(dataprocessing)
endif()
