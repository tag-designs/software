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

if(MSVC)
  find_program(
    STM32_PROGRAMMER
    NAMES STM32_Programmer_CLI STM32_Programmer_CLI.exe
    PATHS
      "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin")
else()
  find_program(
    STM32_PROGRAMMER
    NAMES STM32_Programmer_CLI
    HINTS
      "/Users/geobrown/Software/stm32cubeprogrammer/STM32CubeProgrammer.app/Contents/MacOs/bin"
    PATHS "/home/geobrown/Software/STM32CubeProgrammer/bin"
  )
endif()

if(NOT DEFINED STM32_PROGRAMMER)
  message(
    "To program embedded boards install https://www.st.com/en/development-tools/stm32cubeprog.html"
  )
endif()

get_filename_component(STM32_PROGRAMMER_BIN_DIR ${STM32_PROGRAMMER} DIRECTORY)
SET(STM32_PROGRAMMER_API_INCLUDE_DIR "${STM32_PROGRAMMER_BIN_DIR}/../api/include")
SET(STM32_PROGRAMMER_API_LIB_DIR "${STM32_PROGRAMMER_BIN_DIR}/../api/lib/x64")

message(${STM32_PROGRAMMER_BIN_DIR})
message(${STM32_PROGRAMMER_API_INCLUDE_DIR})
message(${STM32_PROGRAMMER_API_LIB_DIR})

find_library(CUBE_PROGRAMMER_API_LIB 
             NAMES "CubeProgrammer_API" 
             PATHS ${STM32_PROGRAMMER_API_LIB_DIR}
                   ${STM32_PROGRAMMER_BIN_DIR})

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
    download.cpp
    logwindow.cpp
    main.cpp
    mainwindow.cpp)

qt6_wrap_ui(UI_HEADERS logwindow.ui mainwindow.ui)

# Tell CMake to create the qtmonitor executable

add_executable(
  qtprogram MACOSX_BUNDLE WIN32 ${qtprogram_SRCS} ${UI_HEADERS} ${PROTO_HDRS}
)# resources/resources.qrc)

target_include_directories(qtprogram PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

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

target_link_libraries(qtprogram PRIVATE tag Qt6::Widgets ${CCUBE_PROGRAMMER_API_LIB})
target_include_directories(qtprogram PRIVATE ${STM32_PROGRAMMER_API_INCLUDE_DIR})

if(WIN32)
  # why the opengle extensions ????
  #target_link_libraries(qtprogram) )
endif()
