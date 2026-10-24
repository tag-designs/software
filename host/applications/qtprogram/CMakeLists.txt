#[[
  qtprogram (Tag Programmer): a station for programming a batch of tags. It
  writes firmware with STMicroelectronics' STM32_Programmer_CLI, which it finds
  at run time (it is not a build dependency), tests each tag through its USB
  base, and records every attempt in a CSV.

  Links tagcore (the tag protocol, and the ELF and identity-record readers
  under recovery/) and host_common (HostStyle, the user-guide finder). Needs
  Qt6 Widgets and Network (the GitHub release listing); without Qt6 the target
  is skipped. Extracting a release runs the system `tar` at run time. See
  README.md.
#]]
cmake_minimum_required(VERSION 3.15)

message("Configure qtprogram")
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

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR})

# Find the QtWidgets library

find_package(Qt6 COMPONENTS Core Widgets Network CONFIG)
if(NOT Qt6_FOUND)
  message("Qt6 Not found")
  message("Cannot Build qtprogram")
  return()
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

set(qtprogram_SRCS
    firmwarecatalog.cpp
    firmwareimage.cpp
    flashplan.cpp
    logwindow.cpp
    main.cpp
    mainwindow.cpp
    programmercli.cpp
    programsession.cpp
    recordlog.cpp
    station.cpp)

qt6_wrap_ui(UI_HEADERS logwindow.ui)

# Tell CMake to create the qtmonitor executable

add_executable(
  qtprogram MACOSX_BUNDLE WIN32 ${qtprogram_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(qtprogram PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# The source tree, for --capture-screenshots' default output directory, and
# the host package version, recorded in every CSV row.
target_compile_definitions(
  qtprogram PRIVATE TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                    QTPROGRAM_HOST_VERSION="${ULTRALIGHT_PACKAGE_VERSION}")

set_target_properties(
  qtprogram
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

if(APPLE)
  set_target_properties(qtprogram PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
  # windeployqt(qtprogram)
endif()

# Use the Widgets module from Qt 5

target_link_libraries(qtprogram PRIVATE tagcore host_common Qt6::Widgets Qt6::Network)

if(WIN32)
  # why the opengle extensions ????
  #target_link_libraries(qtprogram) )
endif()

#[[
  @brief  qtprogram_checks: offline assertion checks for the station pieces.

  @details Checks the STM32_Programmer_CLI output parsers, the record CSV, the
           release-archive member filter, the GitHub release-list parser and,
           given a release archive, the whole download-extract-verify path, with
           no tag, base or network. Off by default because it is a developer
           tool, not a shipped binary. See test/README.md.
#]]
option(BUILD_QTPROGRAM_CHECKS "Build qtprogram's offline checks (developer tool)" OFF)

if(BUILD_QTPROGRAM_CHECKS)
  add_executable(qtprogram_checks
    test/qtprogram_checks.cpp firmwarecatalog.cpp firmwareimage.cpp flashplan.cpp
    programmercli.cpp recordlog.cpp)
  target_include_directories(qtprogram_checks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(qtprogram_checks PRIVATE tagcore Qt6::Core Qt6::Network)
  set_target_properties(qtprogram_checks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
endif()
