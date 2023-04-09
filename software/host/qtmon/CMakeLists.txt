cmake_minimum_required(VERSION 3.15)

message("Configure qtmonitor")
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
  message("Cannot Build QtMonitor")
  return()
endif()

if(WIN32)
  find_package(Qt5OpenGLExtensions CONFIG)
  if(NOT Qt5OpenGLExtensions_FOUND)
    message("QT5 Open GL Extensions not found")
    message("Cannot Build QtMonitor")
    return()
  endif()
endif()

# Populate a CMake variable with the sources

set(qtmonitor_SRCS
    adxl362config.cpp
    bittaglog.cpp
    configtab.cpp
    dataconfig.cpp
    hibernate.cpp
    logscreen.cpp
    main.cpp
    mainwindow.cpp
    pbenumgroup.cpp
    schedule.cpp)

qt6_wrap_ui(UI_HEADERS mainwindow.ui schedule.ui)

# Tell CMake to create the qtmonitor executable

add_executable(
  qtmonitor MACOSX_BUNDLE WIN32 ${qtmonitor_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(qtmonitor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(
  qtmonitor
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

if(APPLE)
  include(DeployQt)
  macdeployqt(qtmonitor)
elseif(WIN32)
  # windeployqt(qtmonitor)
endif()

# Use the Widgets module from Qt 5

target_link_libraries(qtmonitor PRIVATE tag Qt6::Widgets)

if(WIN32)
  # why the opengle extensions ????
  target_link_libraries(qtmonitor PRIVATE Qt5::OpenGLExtensions)
endif()
