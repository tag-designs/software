cmake_minimum_required(VERSION 3.15)

message("Configure Tag Library")

find_package(SQLite3 REQUIRED)

if(WIN32 AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  set(LIBUSB_1_0_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  set(LIBUSB_1_0_INCLUDE_DIR "${LIBUSB_1_0_ROOT}/include/libusb-1.0")
  set(LIBUSB_1_0_RELEASE_LIB "${LIBUSB_1_0_ROOT}/lib/libusb-1.0.lib")
  set(LIBUSB_1_0_DEBUG_LIB "${LIBUSB_1_0_ROOT}/debug/lib/libusb-1.0.lib")
  if(NOT EXISTS "${LIBUSB_1_0_INCLUDE_DIR}/libusb.h")
    message(FATAL_ERROR "libusb headers not found at ${LIBUSB_1_0_INCLUDE_DIR}")
  endif()
  if(NOT EXISTS "${LIBUSB_1_0_RELEASE_LIB}")
    message(FATAL_ERROR "Release libusb library not found at ${LIBUSB_1_0_RELEASE_LIB}")
  endif()
  if(NOT EXISTS "${LIBUSB_1_0_DEBUG_LIB}")
    message(FATAL_ERROR "Debug libusb library not found at ${LIBUSB_1_0_DEBUG_LIB}")
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBUSB_1_0 REQUIRED IMPORTED_TARGET libusb-1.0)
endif()

#
# Create tag library 
#

add_library(tag STATIC)
if(WIN32 AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  target_include_directories(tag PRIVATE "${LIBUSB_1_0_INCLUDE_DIR}")
  target_link_libraries(tag PRIVATE
    "$<$<CONFIG:Debug>:${LIBUSB_1_0_DEBUG_LIB}>"
    "$<$<NOT:$<CONFIG:Debug>>:${LIBUSB_1_0_RELEASE_LIB}>")
else()
  target_link_libraries(tag PRIVATE PkgConfig::LIBUSB_1_0)
endif()
target_compile_features(tag PUBLIC cxx_std_20)

target_sources(tag
    PRIVATE
         tagmonitor.cc 
         tagclass.cc   
         taglogs.cc 
         sqlitelog.cc
         linkadapt.cc
         log.c
         linkadapt.h
    PUBLIC
         log.h
         tagmonitor.h
         tagclass.h
         taglogs.h
         sqlitelog.h
         linkadapt.h
   )

target_include_directories(tag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tag PUBLIC proto)
target_link_libraries(tag PRIVATE tag_monitor_interface)
target_link_libraries(tag PRIVATE SQLite::SQLite3)
