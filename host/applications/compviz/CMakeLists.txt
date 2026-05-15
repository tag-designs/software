cmake_minimum_required(VERSION 3.10)

# Find includes in corresponding build directories

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure compviz")

if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(HOST_SQLITE_INCLUDE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
  set(HOST_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(HOST_SQLITE_INCLUDE_DIRS ${SQLite3_INCLUDE_DIRS})
  set(HOST_SQLITE_TARGET SQLite::SQLite3)
endif()
set(COMPVIZ_SQLITE_LINK_TARGET ${HOST_SQLITE_TARGET})

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

# Create code from a list of Qt designer ui files

set(CMAKE_AUTOMOC ON)

# Instruct CMake to run moc automatically when needed

set(CMAKE_AUTOUIC ON)

# Find the QtWidgets library

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg SvgWidgets Qml Quick
                            QuickWidgets PrintSupport)

if (NOT Qt6_FOUND) 
    message("Qt6 not found")
    message("Cannot build compviz")
    return()
  endif()


# Populate a CMake variable with the sources

set(compviz_SRCS
  compass_sqlite_loader.cpp
  controls.cpp
  main.cpp
  mainwindow.cpp
  tickerdatetimeoffset.cpp
  sqliteload.cpp
  )

set_source_files_properties(controls.cpp PROPERTIES SKIP_AUTOMOC TRUE)



qt_add_executable(compviz MACOSX_BUNDLE WIN32 ${compviz_SRCS} ${UI_HEADERS} )
#qt_import_qml_plugins(compviz)
target_include_directories(compviz PRIVATE FastFIR)
target_include_directories(compviz PRIVATE ${HOST_SQLITE_INCLUDE_DIRS})
set_property(TARGET compviz PROPERTY QT_DEPLOY_QML_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../libraries/sensorui/orientation_frame")
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


target_link_libraries(compviz PRIVATE ${COMPVIZ_SQLITE_LINK_TARGET} host_common sensorui qcustomplot Qt6::Widgets
                               Qt6::Svg Qt6::QuickWidgets Qt6::SvgWidgets 
                               Qt6::Quick Qt6::PrintSupport)
