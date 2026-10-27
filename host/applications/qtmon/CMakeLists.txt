#[[
  qtmonitor: the Qt Widgets application for attaching to a tag through its
  USB base, reading its status, editing and sending its configuration
  (tagconfiguration/), starting, stopping and erasing it, and downloading its
  log.

  Built only with BUILD_QT_APPS; if Qt6 Core and Widgets are not found the
  directory is skipped with a message rather than failing the configure.
  Links tagcore for everything that talks to a tag and host_common for the
  shared header-only Qt helpers (user guide lookup, file dialogs).
  TAG_DESIGNS_SOURCE_DIR is compiled in so the fixture and screenshot options
  work when qtmonitor runs from the build tree. The configuration-probe case
  generator and the core's assertion checks at the end are developer tools
  behind BUILD_QTMON_CHECKS.
#]]
cmake_minimum_required(VERSION 3.15)

message("Configure qtmonitor")

# The Qt-free core, before any Qt setting so none applies to it.
add_subdirectory(core)
# Find includes in corresponding build directories

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Instruct CMake to run moc automatically when needed

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_USE_QTGUI TRUE)
set(QT_USE_QTXML TRUE)

add_definitions(${QT_DEFINITIONS})

set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tagconfiguration)

# Find the QtWidgets library

find_package(Qt6 COMPONENTS Core Widgets CONFIG)
if(NOT Qt6_FOUND)
  message("Qt6 Not found")
  message("Cannot Build QtMonitor")
  return()
else()
  message(STATUS "Qt6 found in directory: ${Qt6_DIR}")
endif()

if(WIN32)
 # find_package(Qt6OpenGLExtensions CONFIG)
  #if(NOT Qt6OpenGLExtensions_FOUND)
  #  message("QT6 Open GL Extensions not found")
   # message("Cannot Build QtMonitor")
   # return()
  #endif()
endif()

# Populate a CMake variable with the sources

set(qtmonitor_SRCS
    configprobe.cpp
    logwindow.cpp
    main.cpp
    mainwindow.cpp
    mainwindow_fixture.cpp
    session/fixturesession.cpp
    session/livetagsession.cpp
    session/tagsession.h
    tagstatusstrip.cpp
    tagconfiguration/adxl362panel.cpp
    tagconfiguration/bittaglogpanel.cpp
    tagconfiguration/configclock.cpp
    tagconfiguration/configtab.cpp
    tagconfiguration/hibernate.cpp
    tagconfiguration/lsm6panel.cpp
    tagconfiguration/pbenumgroup.cpp
    tagconfiguration/schedulepanel.cpp
    )

qt6_wrap_ui(UI_HEADERS tagconfiguration/configtab.ui
                       tagconfiguration/hibernate.ui
                       logwindow.ui
                       mainwindow.ui
                       tagconfiguration/schedule.ui)

# Tell CMake to create the qtmonitor executable

add_executable(
  qtmonitor MACOSX_BUNDLE WIN32 ${qtmonitor_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(
  qtmonitor
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/session"
    "${CMAKE_CURRENT_SOURCE_DIR}/tagconfiguration")

# Screenshot automation is normally launched from the build tree while
# fixtures and generated documentation images live in the source tree.
target_compile_definitions(
  qtmonitor PRIVATE TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

set_target_properties(
  qtmonitor
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

if(APPLE)
  #include(DeployQt)
  #macdeployqt(qtmonitor)
  set_target_properties(qtmonitor PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
  # windeployqt(qtmonitor)
endif()

# Use the Widgets module from Qt 5

target_link_libraries(qtmonitor PRIVATE qtmon_core tagcore host_common Qt6::Widgets)

#[[
  @brief  Developer checks: the configuration probe's case generator,
          qtmon_core_check (the Qt-free core's assertion checks) and
          qtmon_session_check (a hardware check of the live tag session).

  @details `qtmonitor --config-probe` runs the configuration screen over case
           files with no tag attached; config_probe_cases writes those files
           from the tags' default configs and captured fixtures. The cases are
           committed, so this is needed only to change the corpus. Off by
           default because they are developer tools, not shipped binaries.
           qtmon_core_check needs no Qt and no tag; qtmon_session_check needs
           a bench tag and changes it. Run both by hand; see test/README.md.
#]]
option(BUILD_QTMON_CHECKS
       "Build qtmonitor developer checks: probe case generator, core checks"
       OFF)

if(BUILD_QTMON_CHECKS)
  add_executable(config_probe_cases test/config_probe_cases.cc)
  target_link_libraries(config_probe_cases PRIVATE proto)
  set_target_properties(config_probe_cases PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

  # Assertion checks for the Qt-free core; plain C++, no Qt.
  add_executable(qtmon_core_check test/qtmon_core_check.cc)
  target_link_libraries(qtmon_core_check PRIVATE qtmon_core)
  set_target_properties(qtmon_core_check PROPERTIES
    AUTOMOC OFF AUTOUIC OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

  # Hardware check of the live tag session: one whole tag cycle, no window.
  # It changes the tag; see test/README.md before running it.
  add_executable(qtmon_session_check
    test/qtmon_session_check.cpp
    session/livetagsession.cpp
    session/tagsession.h)
  target_include_directories(qtmon_session_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/session")
  target_link_libraries(qtmon_session_check PRIVATE qtmon_core tagcore Qt6::Core)
  set_target_properties(qtmon_session_check PROPERTIES
    AUTOUIC OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
endif()

if(WIN32)
  # why the opengle extensions ????
  target_link_libraries(qtmonitor)# PRIVATE Qt6::OpenGLExtensions)
endif()
