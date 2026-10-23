#[[
  qtprogram (Tag Programmer): writes firmware to a tag with STMicroelectronics'
  STM32_Programmer_CLI, which it finds at run time (it is not a build
  dependency), then tests the tag through its USB base.

  Links tagcore (the tag protocol) and host_common (HostStyle, the user-guide
  finder). Needs Qt6 Widgets; without Qt6 the target is skipped. See README.md.
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

find_package(Qt6 COMPONENTS Core Widgets CONFIG)
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
    custommessagebox.cpp
    flashplan.cpp
    logwindow.cpp
    main.cpp
    mainwindow.cpp)

qt6_wrap_ui(UI_HEADERS logwindow.ui mainwindow.ui)

# Tell CMake to create the qtmonitor executable

add_executable(
  qtprogram MACOSX_BUNDLE WIN32 ${qtprogram_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(qtprogram PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# The source tree, for --capture-screenshots' default output directory.
target_compile_definitions(
  qtprogram PRIVATE TAG_DESIGNS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

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

target_link_libraries(qtprogram PRIVATE tagcore host_common Qt6::Widgets)

if(WIN32)
  # why the opengle extensions ????
  #target_link_libraries(qtprogram) )
endif()
