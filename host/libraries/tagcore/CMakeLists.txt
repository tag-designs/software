cmake_minimum_required(VERSION 3.15)

message("Configure TagCore Library")

option(TAGCORE_ENABLE_INSTRUMENTATION
       "Enable host-side tagcore transfer timing instrumentation" ON)

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
target_compile_definitions(tagcore PUBLIC
  TAGCORE_ENABLE_INSTRUMENTATION=$<BOOL:${TAGCORE_ENABLE_INSTRUMENTATION}>)

target_sources(tagcore
    PRIVATE
         tagmonitor.cc 
         legacy_bittag_rescue.cc
         tagclass.cc   
         taglogwriter.cc
         txtlogs.cc
         sqlitelog.cc
         sqlitelog/bittag.cc
         sqlitelog/compasstag.cc
         sqlitelog/imutag.cc
         sqlitelog/pressure.cc
         sqlitelog/schema.cc
         sqlitelog/uiuctag.cc
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
target_link_libraries(tagcore PUBLIC tag_monitor_interface)
target_link_libraries(tagcore PRIVATE ${HOST_SQLITE_TARGET})

#[[
  @brief  Offline assertion checks for the tagcore log decoders.

  @details These drive the decoders with synthetic payloads instead of a tag, so
           a log format can be developed and regression checked without
           hardware. They are off by default because they are developer tools
           rather than shipped binaries, and because they are plain assertion
           programs rather than a registered test framework: build with
           -DBUILD_TAGCORE_CHECKS=ON and run them by hand. See test/README.md.
#]]
option(BUILD_TAGCORE_CHECKS
       "Build the offline tagcore decoder checks (developer tool, run manually)"
       OFF)

if(BUILD_TAGCORE_CHECKS)
  foreach(check uiuctag_format_check uiuctag_decoder_check
                uiuctag_end_to_end_check)
    add_executable(${check} test/${check}.cc)
    target_link_libraries(${check} PRIVATE tagcore tag_monitor_interface)
    set_target_properties(${check} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  endforeach()
endif()
