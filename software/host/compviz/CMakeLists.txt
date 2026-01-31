cmake_minimum_required(VERSION 3.10)

# Find includes in corresponding build directories

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(FastFIR)

message("Configure compviz")

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Create code from a list of Qt designer ui files

set(CMAKE_AUTOMOC ON)

# Instruct CMake to run moc automatically when needed

set(CMAKE_AUTOUIC ON)

# Find the QtWidgets library

find_package(Qt6 COMPONENTS Core Widgets Svg SvgWidgets Qml Quick QuickWidgets PrintSupport CONFIG)

if (NOT Qt6_FOUND) 
    message("Qt6 not found")
    message("Cannot build compviz")
    return()
  endif()


# Populate a CMake variable with the sources

set(compviz_SRCS
  compassdata.cpp 
  constants_dialog.cpp
  controls.cpp
  FastFIR/FastFIR/qjfastfir.cpp
  main.cpp
  mainwindow.cpp
  qcustomplot-source/qcustomplot.cpp
  tickerdatetimeoffset.cpp
  )

set_source_files_properties(compassdata.cpp controls.cpp PROPERTIES SKIP_AUTOMOC TRUE)

qt_add_resources(compviz_SRCS qfi.qrc)



qt_add_executable(compviz MACOSX_BUNDLE WIN32 ${compviz_SRCS} ${UI_HEADERS} )
#qt_import_qml_plugins(compviz)
target_include_directories(compviz PRIVATE FastFIR qcustomplot-source)
set_target_properties(compviz
    PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin
)



if (APPLE)
#include(DeployQt)
set_target_properties(compviz PROPERTIES MACOSX_BUNDLE TRUE)
#)
#macdeployqt(compviz)
elseif(WIN32)
#windeployqt(compviz)
endif()


target_link_libraries(compviz PRIVATE tag Qt6::Widgets Qt6::Svg Qt6::QuickWidgets Qt6::SvgWidgets Qt6::Quick Qt6::PrintSupport)



