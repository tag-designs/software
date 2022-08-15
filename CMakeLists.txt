cmake_minimum_required(VERSION 3.15)

project(NanoTag VERSION 0.1.0)
#set(VCPKG_VERBOSE true)

set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/")
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

add_subdirectory(software)
add_subdirectory(hardware)
add_subdirectory(docs)



