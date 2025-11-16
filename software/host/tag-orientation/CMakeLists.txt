cmake_minimum_required(VERSION 3.15)

message("Configure tag-orientation")
# Find includes in corresponding build directories

set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Instruct CMake to run moc automatically when needed

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_definitions(${QT_DEFINITIONS})

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR})

# Find the QtWidgets library


#if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
#message("Building on Linux")
#set(Qt6_Root "$ENV{HOME}/Qt/6.8.3/gcc_64/bin")
#set(Qt6_DIR "ENV{HOME}/Qt/6.8.3/gcc_64/lib/cmake/Qt6/")
#endif()
find_package(Qt6 COMPONENTS Core GUI CONFIG)   #DataVisualization
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

set(tag-orientation_SRCS
    ../qtcalibrate/compassdata.cpp
    ../qtcalibrate/magcal.c
    main.cpp
    MainClass.cpp
    ../qtcalibrate/matrix.c
    ../qtcalibrate/quality.c
    )


# Tell CMake to create the qtmonitor executable

add_executable(
  tag-orientation MACOSX_BUNDLE WIN32 ${tag-orientation_SRCS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(tag-orientation  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "../qtcalibrate")

set_target_properties(
  tag-orientation
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
             RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)

if(APPLE)
  set_target_properties(tag-orientation PROPERTIES MACOSX_BUNDLE TRUE)
elseif(WIN32)
  # windeployqt(qtprogram)
endif()

# Use the Widgets module from Qt 5

target_link_libraries(tag-orientation PRIVATE tag Qt6::Core Qt6::Gui) 
if(WIN32)
  # why the opengle extensions ????
  #target_link_libraries(qtprogram) )
endif()
