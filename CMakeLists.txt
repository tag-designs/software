cmake_minimum_required(VERSION 3.20)

project(NanoTag VERSION 0.1.0)
#set(VCPKG_VERBOSE true)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.3" CACHE STRING "Minimum OS X deployment version")
#set(CMAKE_OSX_SYSROOT "/Users/geobrown/Software/macos-sdk/MacOSX12.3.sdk")
include(CMakePrintHelpers)

if(WIN32)
  set(_build_embedded_default OFF)
else()
  set(_build_embedded_default ON)
endif()

if(MSVC)
  set(
    CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    CACHE STRING
    "Use the dynamic MSVC runtime")
  find_program(rc rc.exe)
  add_link_options(/VERBOSE:LIB)
endif()

option(BUILD_HOST "Build host libraries and tools" ON)
option(BUILD_QT_APPS "Build Qt host applications" ON)
option(BUILD_HOST_DOCS "Build host application user guides by default and install them with host packages" ${BUILD_QT_APPS})
option(BUILD_EMBEDDED "Build embedded firmware targets" ${_build_embedded_default})
option(MACOS_SIGN_APPS "Sign macOS app bundles while packaging" ON)
option(MACOS_USE_MACDEPLOYQT_DEPLOYMENT "Let macdeployqt handle macOS bundle deployment" ON)
option(MACOS_DEPLOY_APPSTORE_COMPLIANT "Ask macdeployqt to skip app-store-incompatible plugins" ON)
option(MACOS_DEPLOY_QT_PLUGINS_MANUALLY "Deploy a curated macOS Qt plugin set instead of letting macdeployqt scan all plugins" ON)
option(MACOS_FIXUP_BUNDLE_DEPENDENCIES "Copy and rewrite non-system dylib dependencies into macOS app bundles" ON)

set(
  HOST_MKDOCS_COMMAND
  ""
  CACHE STRING
        "Command used to run MkDocs, as a CMake list; for example python;-m;mkdocs")

set(HOST_DOCS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/host/docs")
set(HOST_DOCS_CONFIG "${HOST_DOCS_SOURCE_DIR}/mkdocs.yml")
set(HOST_DOCS_BUILD_DIR "${CMAKE_BINARY_DIR}/host/docs/site")
set(HOST_DOCS_INSTALL_SCRIPT "${CMAKE_BINARY_DIR}/host/BuildHostDocs.cmake")

if(NOT HOST_MKDOCS_COMMAND)
  find_program(MKDOCS_EXECUTABLE NAMES mkdocs mkdocs.exe)
  if(MKDOCS_EXECUTABLE)
    set(
      HOST_MKDOCS_COMMAND
      "${MKDOCS_EXECUTABLE}"
      CACHE STRING
            "Command used to run MkDocs, as a CMake list; for example python;-m;mkdocs"
            FORCE)
  else()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      set(
        HOST_MKDOCS_COMMAND
        "${Python3_EXECUTABLE};-m;mkdocs"
        CACHE STRING
              "Command used to run MkDocs, as a CMake list; for example python;-m;mkdocs"
              FORCE)
    endif()
  endif()
endif()

if(NOT HOST_MKDOCS_COMMAND AND BUILD_HOST_DOCS)
  message(FATAL_ERROR "BUILD_HOST_DOCS=ON requires MkDocs. Install the tools with `python3 -m pip install -r host/docs/requirements.txt`, set HOST_MKDOCS_COMMAND to a CMake command list such as `python;-m;mkdocs`, or configure with -DBUILD_HOST_DOCS=OFF.")
endif()

file(
  GLOB_RECURSE host_docs_sources
  CONFIGURE_DEPENDS
  "${HOST_DOCS_CONFIG}"
  "${HOST_DOCS_SOURCE_DIR}/src/*")

set(host_docs_all)
if(BUILD_HOST_DOCS)
  set(host_docs_all ALL)
endif()

if(HOST_MKDOCS_COMMAND)
  string(REPLACE ";" " " HOST_MKDOCS_COMMAND_DISPLAY "${HOST_MKDOCS_COMMAND}")
  message(STATUS "Using MkDocs command: ${HOST_MKDOCS_COMMAND_DISPLAY}")

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/BuildHostDocs.cmake.in"
    "${HOST_DOCS_INSTALL_SCRIPT}"
    @ONLY)

  add_custom_target(
    docs ${host_docs_all}
    COMMAND
      ${HOST_MKDOCS_COMMAND} build
      --config-file "${HOST_DOCS_CONFIG}"
      --site-dir "${HOST_DOCS_BUILD_DIR}"
      --clean
    DEPENDS ${host_docs_sources}
    WORKING_DIRECTORY "${HOST_DOCS_SOURCE_DIR}"
    COMMENT "Building host application user guide"
    VERBATIM)
else()
  add_custom_target(
    docs
    COMMAND
      ${CMAKE_COMMAND} -E echo
      "MkDocs is required to build host docs. Install with: python3 -m pip install -r host/docs/requirements.txt"
    COMMAND ${CMAKE_COMMAND} -E false
    COMMENT "MkDocs is not available for host documentation"
    VERBATIM)
endif()

add_custom_target(host_docs DEPENDS docs)

set(
  MACOS_CODE_SIGN_IDENTITY
  "Developer ID Application: Indiana University (5J69S77A7G)"
  CACHE STRING
  "codesign identity used for macOS app bundles")
set(
  MACOS_CODE_SIGN_ENTITLEMENTS
  ""
  CACHE FILEPATH
  "Optional entitlements plist used when signing macOS app bundles")
set(
  MACOS_MACDEPLOYQT_EXTRA_OPTIONS
  ""
  CACHE STRING
  "Extra options passed to macdeployqt during package staging")
set(
  MACOS_BUNDLE_FIXUP_EXTRA_DIRS
  ""
  CACHE STRING
  "Extra library directories searched by fixup_bundle while packaging macOS apps")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()
list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

# Find Protobuf and Nanopb

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

# create common include for low-level monitor

add_library(tag_monitor_interface INTERFACE)
target_include_directories(tag_monitor_interface INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")

if(WIN32)
  set(CMAKE_INSTALL_BINDIR "tag_tools" CACHE PATH "Windows runtime tools directory" FORCE)
  install(CODE [[
    if(NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "Release")
      message(FATAL_ERROR "Windows install/package is Release-only. Build Debug with `cmake --build --preset debug`, but install/package with Release.")
    endif()
  ]])
endif()

set(CMAKE_FIND_ROOT 
    "/home/geobrown/Software;/Users/geobrown/Software;c:/Program Files/fmpp/bin;C:/Program Files/STMicroelectronics" )

#set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

set(ULTRALIGHT_PACKAGE_FALLBACK_VERSION "2.0.0")
set(ULTRALIGHT_PACKAGE_GIT_TAG "")
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" tag --merged HEAD --list "v[0-9]*.[0-9]*" --sort=-v:refname
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE _ultralight_git_tag_result
    OUTPUT_VARIABLE _ultralight_git_tags
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_ultralight_git_tag_result EQUAL 0 AND _ultralight_git_tags)
    string(REPLACE "\n" ";" _ultralight_git_tag_list "${_ultralight_git_tags}")
    foreach(_ultralight_git_tag IN LISTS _ultralight_git_tag_list)
      if(_ultralight_git_tag MATCHES "^v[0-9]+\\.[0-9]+$")
        set(ULTRALIGHT_PACKAGE_GIT_TAG "${_ultralight_git_tag}")
        break()
      endif()
    endforeach()
  endif()
endif()
if(NOT ULTRALIGHT_PACKAGE_GIT_TAG)
  string(
    REGEX REPLACE
    "^([0-9]+)\\.([0-9]+).*$"
    "v\\1.\\2"
    ULTRALIGHT_PACKAGE_GIT_TAG
    "${ULTRALIGHT_PACKAGE_FALLBACK_VERSION}")
endif()
string(REGEX REPLACE "^v" "" ULTRALIGHT_PACKAGE_VERSION "${ULTRALIGHT_PACKAGE_GIT_TAG}")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)$" _ultralight_package_version_match "${ULTRALIGHT_PACKAGE_VERSION}")
set(ULTRALIGHT_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(ULTRALIGHT_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
message(STATUS "UltralightTags package tag: ${ULTRALIGHT_PACKAGE_GIT_TAG}")

set(CPACK_PACKAGE_NAME "UltralightTags")
set(CPACK_PACKAGE_VENDOR "cs.iu.edu")
set(CPACK_PACKAGE_VERSION "${ULTRALIGHT_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${ULTRALIGHT_PACKAGE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${ULTRALIGHT_PACKAGE_VERSION_MINOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ultralight Tags Host Tools")

add_subdirectory(proto)
if(BUILD_HOST)
  add_subdirectory(host)
endif()
if(BUILD_EMBEDDED)
  add_subdirectory(embedded)
endif()

if(WIN32)
  set(CPACK_GENERATOR ZIP)
endif()

if(APPLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_PACKAGE_FILE_NAME "Ultralight-tags-${ULTRALIGHT_PACKAGE_GIT_TAG}")
  if(MACOS_SIGN_APPS AND MACOS_CODE_SIGN_IDENTITY)
    set(CPACK_BUNDLE_APPLE_CERT_APP "${MACOS_CODE_SIGN_IDENTITY}")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${MACOS_CODE_SIGN_IDENTITY}")
  endif()
endif()

include(CPack)
