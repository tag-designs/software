#[[
  tagcore: the static C++20 library every host tool uses to talk to a tag.

  It owns the USB link to a base (libusb), the tag monitor protocol
  (tagmonitor, tagclass), log download and the SQLite/text log writers, the
  SWD capture and recovery code under recovery/, and ConfigShape, which says
  which Config fields each tag type supports.

  Dependencies that are easy to miss:
  - libusb-1.0: on Windows with vcpkg the import libraries are linked by
    path, Debug and Release separately; elsewhere it is found through
    pkg-config, statically when building with vcpkg (see the comment at
    pkg_check_modules).
  - sqlite3: vcpkg's unofficial-sqlite3 package when building with vcpkg, the
    system SQLite3 otherwise.
  - proto (the generated tag.pb.* library) and tag_monitor_interface (the
    shared C headers in include/) are PUBLIC: consumers see their headers.
  - embedded/proto-c/*/default-config.json are read at configure time and
    compiled in (see the defaultconfigs.cc block below), so a configure, not
    just a build, picks up a new tag's defaults.
#]]
cmake_minimum_required(VERSION 3.15)

message("Configure TagCore Library")

# Times every USB bulk transfer in LinkAdapt and keeps per-direction call,
# byte and nanosecond counters. Cheap (one steady_clock read per transfer),
# so it is on by default; turn it off to drop the clock calls entirely.
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
  # vcpkg's libusb is a static library on macOS and Linux, so it needs the
  # private dependencies its .pc lists: IOKit and CoreFoundation on macOS,
  # libudev and pthread on Linux.
  if(VCPKG_TARGET_TRIPLET)
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
         configshape.cc
         log.c
         linkadapt.h
         recovery/sha256.cc
         recovery/swdmcu.cc
         recovery/swdsession.cc
         recovery/targetimage.cc
         recovery/sramcall.cc
         recovery/externalflash.cc
         recovery/externalcapture.cc
         recovery/identityrecord.cc
         recovery/statecapture.cc
         recovery/capturesource.cc
    PUBLIC
         log.h
         tagmonitor.h
         tagclass.h
         taglogwriter.h
         txtlogs.h
         sqlitelog.h
         linkadapt.h
         configshape.h
         recovery/sha256.h
         recovery/swdmcu.h
         recovery/swdsession.h
         recovery/targetimage.h
         recovery/sramcall.h
         recovery/externalflash.h
         recovery/externalcapture.h
         recovery/identityrecord.h
         recovery/statecapture.h
         recovery/capturesource.h
   )

target_include_directories(tagcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#[[
  @brief  Embeds every tag's default-config.json into tagcore, for ConfigShape.

  @details ConfigShape reads which Config fields each tag type supports from
           embedded/proto-c/<name>-proto-c/default-config.json. This copies the
           text of each file into a generated defaultconfigs.cc as a raw string
           literal; it does not interpret the JSON, so it never changes when a
           field is added. The files are configure dependencies, so editing one
           or adding a tag reruns this, and the output is rewritten only when
           its content changes.
#]]
file(GLOB TAGCORE_DEFAULT_CONFIGS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/embedded/proto-c/*-proto-c/default-config.json")
list(SORT TAGCORE_DEFAULT_CONFIGS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${TAGCORE_DEFAULT_CONFIGS})
set(_defaults_cc "// Generated by host/libraries/tagcore/CMakeLists.txt. Do not edit.\n")
string(APPEND _defaults_cc "#include <cstddef>\n")
string(APPEND _defaults_cc
       "struct EmbeddedDefaultConfig { const char *name; const char *text; };\n")
string(APPEND _defaults_cc "extern const EmbeddedDefaultConfig kEmbeddedDefaultConfigs[] = {\n")
foreach(_json IN LISTS TAGCORE_DEFAULT_CONFIGS)
  get_filename_component(_dir "${_json}" DIRECTORY)
  get_filename_component(_name "${_dir}" NAME)
  file(READ "${_json}" _text)
  string(APPEND _defaults_cc "  {\"${_name}\", R\"json(${_text})json\"},\n")
endforeach()
list(LENGTH TAGCORE_DEFAULT_CONFIGS _count)
if(_count EQUAL 0)
  string(APPEND _defaults_cc "  {nullptr, nullptr},\n")
endif()
string(APPEND _defaults_cc "};\n")
string(APPEND _defaults_cc
       "extern const std::size_t kEmbeddedDefaultConfigCount = ${_count};\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/defaultconfigs.cc.tmp" "${_defaults_cc}")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/defaultconfigs.cc.tmp"
               "${CMAKE_CURRENT_BINARY_DIR}/defaultconfigs.cc" COPYONLY)
target_sources(tagcore PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/defaultconfigs.cc")

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
                uiuctag_end_to_end_check config_shape_check)
    add_executable(${check} test/${check}.cc)
    target_link_libraries(${check} PRIVATE tagcore tag_monitor_interface)
    set_target_properties(${check} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
  endforeach()
endif()
