cmake_minimum_required(VERSION 3.15)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.3" CACHE STRING "Minimum OS X deployment version")
#set(CMAKE_OSX_SYSROOT "/Users/geobrown/Software/macos-sdk/MacOSX12.3.sdk")
include(CMakePrintHelpers)

if (MSVC)
    set(Protobuf_USE_STATIC_LIBS ON)
    find_program(rc rc.exe)
    add_compile_options(
            $<$<CONFIG:>:/MT> #---------|
            $<$<CONFIG:Debug>:/MTd> #---|-- Statically link the runtime libraries
            $<$<CONFIG:Release>:/MT> #--|
        )
     add_link_options(/VERBOSE:LIB)
endif()


# Find Protobuf and Nanopb

set(Protobuf_USE_STATIC_LIBS ON)
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
cmake_print_variables(Protobuf_VERSION)

if(BUILD_EMBEDDED)
    set(NANOPB_SRC_ROOT_FOLDER "$ENV{NANOPB_ROOT}" CACHE PATH "Nanopb source root")
    if(NOT NANOPB_SRC_ROOT_FOLDER)
        set(NANOPB_SRC_ROOT_FOLDER "${CMAKE_SOURCE_DIR}/nanopb" CACHE PATH "Nanopb source root" FORCE)
    endif()
    set(NANOPB_GENERATOR_SOURCE_DIR "${NANOPB_SRC_ROOT_FOLDER}/generator-bin" CACHE PATH "Nanopb generator directory")
    list(APPEND CMAKE_MODULE_PATH "${NANOPB_SRC_ROOT_FOLDER}/extra/")
endif()
#find_package(Nanopb)

# create common include for low-level monitor

add_library(tag_monitor_interface INTERFACE)
target_include_directories(tag_monitor_interface INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")

# subdirectories

#set(CPACK_BUNDLE_APPLE_CERT_APP "Developer ID Application: Indiana University (5J69S77A7G)")

add_subdirectory(proto)
if(BUILD_HOST)
    add_subdirectory(host)
endif()
if(BUILD_EMBEDDED)
    add_subdirectory(embedded)
endif()
