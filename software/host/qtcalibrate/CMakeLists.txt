cmake_minimum_required(VERSION 3.15)

message("Configure qtcalibrate")
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


#if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
#message("Building on Linux")
#set(Qt6_Root "$ENV{HOME}/Qt/6.8.3/gcc_64/bin")
#set(Qt6_DIR "ENV{HOME}/Qt/6.8.3/gcc_64/lib/cmake/Qt6/")
#endif()
find_package(Qt6 COMPONENTS Core Widgets Svg SvgWidgets Quick QuickWidgets CONFIG)   #DataVisualization
if(NOT Qt6_FOUND)
  message("Qt6 Not found")
  message("Cannot Build qtcalibrate")
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

set(qtcalibrate_SRCS
    compassdata.cpp
    magcal.c
    magplot.cpp
    main.cpp
    mainwindow.cpp
    matrix.c
    #myscatter3d.cpp
    #qfi_AI.cpp
    #qfi_HI.cpp
    quality.c
    sinbin.cpp)

qt6_wrap_ui(UI_HEADERS 
            mainwindow.ui)


qt_add_resources(qtcalibrate_SRCS qfi.qrc)


# Tell CMake to create the qtmonitor executable

add_executable(
  qtcalibrate MACOSX_BUNDLE WIN32 ${qtcalibrate_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(qtcalibrate PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(
  qtcalibrate
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

if(APPLE)
  set_target_properties(qtcalibrate PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
  # windeployqt(qtprogram)
endif()

# Use the Widgets module from Qt 5

target_link_libraries(qtcalibrate PRIVATE tag Qt6::Core Qt6::Widgets Qt6::Svg Qt6::QuickWidgets Qt6::SvgWidgets Qt6::Quick)  # Qt6::DataVisualization )#
if(WIN32)
  # why the opengle extensions ????
  #target_link_libraries(qtprogram) )
endif()
