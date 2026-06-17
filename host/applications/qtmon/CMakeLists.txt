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

# Generate config field visibility from tag default-config.json files. The
# generated table preserves JSON field presence, including scalar zero values
# that are lost as presence signals after proto3 parsing.
file(GLOB CONFIG_VISIBILITY_JSONS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/embedded/proto-c/*-proto-c/default-config.json")
set(CONFIG_VISIBILITY_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/generated_configfieldvisibility.cpp")
add_custom_command(
  OUTPUT "${CONFIG_VISIBILITY_SOURCE}"
  COMMAND "${CMAKE_COMMAND}"
          "-DOUTPUT=${CONFIG_VISIBILITY_SOURCE}"
          "-DINPUTS=${CONFIG_VISIBILITY_JSONS}"
          -P "${CMAKE_SOURCE_DIR}/cmake/GenerateConfigVisibility.cmake"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/cmake/GenerateConfigVisibility.cmake"
    ${CONFIG_VISIBILITY_JSONS}
  VERBATIM)

# Populate a CMake variable with the sources

set(qtmonitor_SRCS
    ${CONFIG_VISIBILITY_SOURCE}
    abstractdownload.cpp
    logwindow.cpp
    main.cpp
    mainwindow.cpp
    tagconfiguration/adxl362config.cpp
    tagconfiguration/bittaglog.cpp
    tagconfiguration/configtab.cpp
    tagconfiguration/hibernate.cpp
    tagconfiguration/pbenumgroup.cpp
    tagconfiguration/schedule.cpp
    tagconfiguration/lsm6dsvconfig.cpp
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
    "${CMAKE_CURRENT_SOURCE_DIR}/tagconfiguration")

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

target_link_libraries(qtmonitor PRIVATE tagcore host_common Qt6::Widgets)

if(WIN32)
  # why the opengle extensions ????
  target_link_libraries(qtmonitor)# PRIVATE Qt6::OpenGLExtensions)
endif()
