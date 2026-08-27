cmake_minimum_required(VERSION 3.15)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

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

  # bundle things up properly

  if(APPLE)
    #include(DeployQt)
    #macdeployqt(${name})
  endif()
endfunction()


host_cli_tool(tag-dwnld MACOSX_BUNDLE WIN32 dwnld.cc options.cc)
host_cli_tool(tag-test MACOSX_BUNDLE WIN32 test.cc options.cc)
host_cli_tool(tag-test-example MACOSX_BUNDLE WIN32 test-example.cc options.cc)
host_cli_tool(tag-info MACOSX_BUNDLE WIN32 tag-info.cc options.cc)
host_cli_tool(tag-reset MACOSX_BUNDLE WIN32 tag-reset.cc options.cc)
host_cli_tool(tag-start MACOSX_BUNDLE WIN32 tag-start.cc options.cc)
host_cli_tool(tag-stop MACOSX_BUNDLE WIN32 tag-stop.cc options.cc)
host_cli_tool(tag-cal MACOSX_BUNDLE WIN32 tag-calibrate.cc options.cc)
host_cli_tool(tag-monitor-test MACOSX_BUNDLE WIN32 tag-monitor-test.cc options.cc)
host_cli_tool(tag-attach-cycle MACOSX_BUNDLE WIN32 tag-attach-cycle.cc options.cc)
host_cli_tool(qtmonitor-fixture-capture MACOSX_BUNDLE WIN32 qtmonitor-fixture-capture.cc)

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
  target_link_libraries(dataprocessing PRIVATE sensoranalysis ${DATAPROCESSING_SQLITE_TARGET})
  set_target_properties(
    dataprocessing
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
               RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  if(WIN32)
    set_target_properties(dataprocessing PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)
  endif()
endif()
