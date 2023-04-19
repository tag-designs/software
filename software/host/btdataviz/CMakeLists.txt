cmake_minimum_required(VERSION 3.10)

# Find includes in corresponding build directories

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(FastFIR)

message("Configure btviz")

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Create code from a list of Qt designer ui files

set(CMAKE_AUTOMOC ON)

# Instruct CMake to run moc automatically when needed

set(CMAKE_AUTOUIC ON)

# Find the QtWidgets library

find_package(Qt6 COMPONENTS Core Widgets PrintSupport CONFIG)

if (NOT Qt6_FOUND) 
    message("Qt6 not found")
    message("Cannot build btviz")
    return()
  endif()


# Populate a CMake variable with the sources

set(btviz_SRCS
  mainwindow.cpp
  qcustomplot-source/qcustomplot.cpp
  actogram.cpp
  tickerdatetimeoffset.cpp
  solpos.c
  FastFIR/FastFIR/qjfastfir.cpp
  main.cpp)


add_executable(btviz MACOSX_BUNDLE WIN32 ${btviz_SRCS} ${UI_HEADERS} )
target_include_directories(btviz PRIVATE FastFIR qcustomplot-source)
set_target_properties(btviz
    PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
)



if (APPLE)
include(DeployQt)
set_target_properties(btviz PROPERTIES
    MACOSX_BUNDLE TRUE
)
#macdeployqt(btviz)
elseif(WIN32)
#windeployqt(btviz)
endif()

target_link_libraries(btviz PRIVATE Qt6::Widgets Qt6::PrintSupport)


