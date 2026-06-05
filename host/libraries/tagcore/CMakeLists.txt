cmake_minimum_required(VERSION 3.15)

message("Configure TagCore Library")

if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  set(HOST_SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(HOST_SQLITE_TARGET SQLite::SQLite3)
endif()

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
  set(_pkg_config_argn_saved "${PKG_CONFIG_ARGN}")
  if(APPLE AND VCPKG_TARGET_TRIPLET)
    set(PKG_CONFIG_ARGN --static)
  endif()
  pkg_check_modules(LIBUSB_1_0 REQUIRED IMPORTED_TARGET libusb-1.0)
  if(APPLE AND VCPKG_TARGET_TRIPLET AND TARGET PkgConfig::LIBUSB_1_0)
    # CMake's pkg-config importer can turn repeated -framework flags from
    # static .pc files into malformed leading link options. The framework
    # entries are already present in the imported target's link libraries.
    set_property(TARGET PkgConfig::LIBUSB_1_0 PROPERTY INTERFACE_LINK_OPTIONS "")
  endif()
  set(PKG_CONFIG_ARGN "${_pkg_config_argn_saved}")
endif()

#
# Create tagcore library
#

add_library(tagcore STATIC)
if(WIN32 AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  target_include_directories(tagcore PRIVATE "${LIBUSB_1_0_INCLUDE_DIR}")
  target_link_libraries(tagcore PRIVATE
    "$<$<CONFIG:Debug>:${LIBUSB_1_0_DEBUG_LIB}>"
    "$<$<NOT:$<CONFIG:Debug>>:${LIBUSB_1_0_RELEASE_LIB}>")
else()
  target_link_libraries(tagcore PRIVATE PkgConfig::LIBUSB_1_0)
endif()
target_compile_features(tagcore PUBLIC cxx_std_20)

target_sources(tagcore
    PRIVATE
         tagmonitor.cc 
         tagclass.cc   
         taglogwriter.cc
         txtlogs.cc
         sqlitelog.cc
         sqlitelog/imutag.cc
         sqlitelog/schema.cc
         linkadapt.cc
         log.c
         linkadapt.h
    PUBLIC
         log.h
         tagmonitor.h
         tagclass.h
         taglogwriter.h
         txtlogs.h
         sqlitelog.h
         linkadapt.h
   )

target_include_directories(tagcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tagcore PUBLIC proto)
target_link_libraries(tagcore PRIVATE tag_monitor_interface)
target_link_libraries(tagcore PRIVATE ${HOST_SQLITE_TARGET})
