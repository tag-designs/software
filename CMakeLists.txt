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
option(BUILD_DEVELOPER_DOCS "Build developer architecture and API documentation by default" OFF)
option(BUILD_EMBEDDED "Build embedded firmware targets" ${_build_embedded_default})
option(REPRODUCIBLE_BUILD
       "Treat conditions that make a build unreproducible as errors rather than warnings"
       OFF)
set(REGENERATE_SOURCES "AUTO" CACHE STRING
    "Regenerate committed generated sources: AUTO (when their recorded inputs change), ON (always), OFF (never)")
set_property(CACHE REGENERATE_SOURCES PROPERTY STRINGS AUTO ON OFF)
set(ARM_TOOLCHAIN_VERSION "" CACHE STRING
    "Expected arm-none-eabi-gcc version; empty records the version without checking it")
string(TOUPPER "${REGENERATE_SOURCES}" REGENERATE_SOURCES)
if(NOT REGENERATE_SOURCES MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR
    "REGENERATE_SOURCES must be AUTO, ON or OFF, not '${REGENERATE_SOURCES}'.")
endif()
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

set(DEVELOPER_DOCS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/docs/developer")
set(DEVELOPER_DOCS_STAGING_DIR "${CMAKE_BINARY_DIR}/docs/developer-src")
set(DEVELOPER_DOCS_CONFIG "${DEVELOPER_DOCS_STAGING_DIR}/mkdocs.yml")
set(DEVELOPER_DOCS_SITE_DIR "${CMAKE_BINARY_DIR}/docs/developer/site")
set(DEVELOPER_DOXYGEN_OUTPUT_DIR "${DEVELOPER_DOCS_SITE_DIR}")
set(DEVELOPER_DOXYGEN_CONFIG "${CMAKE_BINARY_DIR}/docs/Doxyfile.developer")
set(DEVELOPER_DOXYGEN_WARN_LOG "${CMAKE_BINARY_DIR}/docs/developer-doxygen-warnings.log")
set(DEVELOPER_DOCS_STAGE_SCRIPT "${CMAKE_BINARY_DIR}/docs/StageDeveloperDocs.cmake")
set(
  DEVELOPER_DOCS_MARKDOWN
  "design/index.md"
  "design/binary-datalogs.md"
  "design/dataprocessing.md"
  "design/field-data-extraction.md"
  "design/tag-build-reproducibility.md"
  "design/windows-build-notes.md"
  "embedded/bases/BUILD_SOURCES.md"
  "embedded/bases/README.md"
  "embedded/bases/notes.md"
  "embedded/bases/tag-breakout-base-l432-v1/Note.md"
  "embedded/boards/README.md"
  "embedded/boards/tools/README.md"
  "embedded/design/build-orientation.md"
  "embedded/tags/BUILD_SOURCES.md"
  "embedded/tags/README.md"
  "embedded/tags/common/README.md"
  "embedded/tags/common/core/README.md"
  "embedded/tags/common/core/i2c-backend-model.md"
  "embedded/tags/common/core/design/lptim-system-timer.md"
  "embedded/tags/common/core/design/stop-milliseconds-lptim-arr-delay.md"
  "embedded/tags/common/modules/README.md"
  "embedded/tags/common/rtc/README.md"
  "embedded/tags/common/sensors/README.md"
  "embedded/tags/common/sensors/imu/design_notes.md"
  "embedded/tags/families/README.md"
  "embedded/tags/design/monitor_interface.md"
  "embedded/tags/design/i2c-bus-recovery.md"
  "embedded/tags/design/debugging.md"
  "embedded/tags/design/open-issues.md"
  "embedded/tags/design/stm32u375-standby-forum-post.md"
  "embedded/tags/design/restart-recovery.md"
  "embedded/tags/design/custom-defines.md"
  "embedded/tags/design/u375-stop-support.md"
  "embedded/tags/design/u375-stop3-terminal-sleep-plan.md"
  "embedded/tags/common/sensors/imu/design/assumptions.md"
  "embedded/tags/common/sensors/mag/README.md"
  "embedded/tags/common/storage/README.md"
  "embedded/tags/common/test/README.md"
  "embedded/tags/IMUTagNand/README.md"
  "embedded/tags/IMUTagNand/todo.md"
  "embedded/tags/IMUTagNandBmp581/README.md"
  "embedded/tags/IMUTagNandBmp581/todo.md"
  "embedded/tags/CompassTagAT25Breakout/design/notes.md"
  "embedded/tags/UIUCTag/test/README.md"
  "embedded/tags/families/BitPresTag/README.md"
  "embedded/tags/families/BitPresTag/design/bmp581-forced-mode.md"
  "embedded/tags/families/BitPresTag/design/uiuctag-board-integration.md"
  "embedded/tags/families/BitPresTag/design/uiuctag-data-collection.md"
  "embedded/tags/families/BitPresTag/design/uiuctag-test-strategy.md"
  "embedded/tags/families/BitPresTag/design/uiuctag-bringup-report.md"
  "embedded/tags/families/BitTagNG/README.md"
  "embedded/tags/families/BitTagNG/wakeup_note.md"
  "embedded/tags/families/CompassTag/README.md"
  "embedded/tags/families/IMUTag/README.md"
  "embedded/tags/families/IMUTag/design/jitter-free-sampling-timing-reconstruction.md"
  "embedded/tags/families/IMUTag/design/internal-header-checkpoints.md"
  "embedded/tags/families/IMUTag/design/imutag-nand-bmp581-development-plan.md"
  "embedded/tags/families/IMUTag/design/start-abort-diagnostics.md"
  "embedded/tags/families/IMUTag/design/PowerEstimates.md"
  "embedded/tags/families/PresTag/README.md"
  "embedded/tags/families/PresTag/design/power-test-plan.md"
  "embedded/tags/families/PresTag/design/power-test-report.md"
  "embedded/tags/families/PresTag/design/power-test-results.md"
  "embedded/tags/families/CompassTag/design/power-test-plan.md"
  "embedded/tags/families/CompassTag/design/power-test-report.md"
  "embedded/tags/families/CompassTag/design/power-test-results.md"
  "host/README.md"
  "host/docs/design/screenshot-automation.md"
  "host/docs/design/qtmonitor-screenshot-automation.md"
  "host/docs/fixtures/qtmonitor/README.md"
  "host/docs/fixtures/sensorviz/README.md"
  "host/applications/README.md"
  "host/applications/qtcalibrate/README.md"
  "host/applications/qtmon/README.md"
  "host/applications/qtmon/tagconfiguration/README.md"
  "host/commandline/README.md"
  "host/common/README.md"
  "host/libraries/README.md"
  "host/libraries/tagcore/README.md"
  "host/libraries/tagcore/design/index.md"
  "host/libraries/tagcore/design/python-interface.md"
  "host/libraries/tagcore/test/README.md"
  "host/libraries/sensoranalysis/README.md"
  "host/libraries/sensorui/README.md"
  "host/applications/sensorviz/README.md"
  "host/applications/sensorviz/design/roadmap.md"
  "host/applications/sensorviz/design/screenshot-capture-plan.md"
  "proto/README.md")

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

file(
  GLOB_RECURSE developer_docs_sources
  CONFIGURE_DEPENDS
  "${DEVELOPER_DOCS_SOURCE_DIR}/*")

set(developer_docs_markdown_sources)
foreach(developer_doc IN LISTS DEVELOPER_DOCS_MARKDOWN)
  list(APPEND developer_docs_markdown_sources "${PROJECT_SOURCE_DIR}/${developer_doc}")
endforeach()

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/StageDeveloperDocs.cmake.in"
  "${DEVELOPER_DOCS_STAGE_SCRIPT}"
  @ONLY)

configure_file(
  "${DEVELOPER_DOCS_SOURCE_DIR}/Doxyfile.in"
  "${DEVELOPER_DOXYGEN_CONFIG}"
  @ONLY)

find_package(Doxygen QUIET)

set(developer_docs_all)
if(BUILD_DEVELOPER_DOCS)
  set(developer_docs_all ALL)
endif()

add_custom_target(
  developer_docs_stage
  COMMAND ${CMAKE_COMMAND} -P "${DEVELOPER_DOCS_STAGE_SCRIPT}"
  DEPENDS ${developer_docs_sources} ${developer_docs_markdown_sources}
  COMMENT "Staging developer documentation sources"
  VERBATIM)

if(DOXYGEN_FOUND)
  add_custom_target(
    api_docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEVELOPER_DOCS_SITE_DIR}"
    COMMAND "${DOXYGEN_EXECUTABLE}" "${DEVELOPER_DOXYGEN_CONFIG}"
    DEPENDS "${DEVELOPER_DOXYGEN_CONFIG}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building developer Doxygen API reference"
    VERBATIM)
else()
  add_custom_target(
    api_docs
    COMMAND
      ${CMAKE_COMMAND} -E echo
      "Doxygen is required to build API docs. Install doxygen and rerun CMake."
    COMMAND ${CMAKE_COMMAND} -E false
    COMMENT "Doxygen is not available for developer API documentation"
    VERBATIM)
endif()

if(HOST_MKDOCS_COMMAND)
  set(developer_docs_doxygen_commands)
  if(DOXYGEN_FOUND)
    set(
      developer_docs_doxygen_commands
      COMMAND ${CMAKE_COMMAND} -E make_directory "${DEVELOPER_DOCS_SITE_DIR}"
      COMMAND "${DOXYGEN_EXECUTABLE}" "${DEVELOPER_DOXYGEN_CONFIG}")
  endif()

  add_custom_target(
    developer_docs ${developer_docs_all}
    COMMAND ${CMAKE_COMMAND} -P "${DEVELOPER_DOCS_STAGE_SCRIPT}"
    COMMAND
      ${HOST_MKDOCS_COMMAND} build
      --config-file "${DEVELOPER_DOCS_CONFIG}"
      --site-dir "${DEVELOPER_DOCS_SITE_DIR}"
      --clean
    ${developer_docs_doxygen_commands}
    DEPENDS ${developer_docs_sources} ${developer_docs_markdown_sources}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building developer documentation portal"
    VERBATIM)
else()
  add_custom_target(
    developer_docs
    COMMAND
      ${CMAKE_COMMAND} -E echo
      "MkDocs is required to build developer docs. Install with: python3 -m pip install -r host/docs/requirements.txt"
    COMMAND ${CMAKE_COMMAND} -E false
    COMMENT "MkDocs is not available for developer documentation"
    VERBATIM)
endif()

add_custom_target(all_docs DEPENDS docs developer_docs)

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

# Report a condition that makes the build unreproducible -- a version
# disagreement, a stale committed source. Warns by default so day-to-day work is
# not obstructed; -DREPRODUCIBLE_BUILD=ON turns the same condition into a
# configure error, which is how CI and release qualification should build.
function(reproducibility_problem)
  string(JOIN "" _message ${ARGN})
  if(REPRODUCIBLE_BUILD)
    message(FATAL_ERROR "${_message}")
  else()
    message(WARNING "${_message} (configure with -DREPRODUCIBLE_BUILD=ON to make this an error)")
  endif()
endfunction()

if(BUILD_EMBEDDED)
  # The nanopb runtime is vendored, because it is compiled into every image.
  # The directory is named for its version, so derive the path by glob and
  # require exactly one: a second directory left behind by a half-finished
  # upgrade must fail rather than let the build pick one.
  file(GLOB NANOPB_RUNTIME_DIRS "${CMAKE_SOURCE_DIR}/embedded/thirdparty/nanopb-*")
  list(LENGTH NANOPB_RUNTIME_DIRS NANOPB_RUNTIME_DIR_COUNT)
  if(NOT NANOPB_RUNTIME_DIR_COUNT EQUAL 1)
    message(FATAL_ERROR
      "Expected exactly one vendored nanopb runtime under "
      "embedded/thirdparty/nanopb-*, found ${NANOPB_RUNTIME_DIR_COUNT}: "
      "${NANOPB_RUNTIME_DIRS}")
  endif()
  list(GET NANOPB_RUNTIME_DIRS 0 NANOPB_RUNTIME_DIR)
  if(NOT EXISTS "${NANOPB_RUNTIME_DIR}/pb.h"
     OR NOT EXISTS "${NANOPB_RUNTIME_DIR}/pb_common.c")
    message(FATAL_ERROR
      "Vendored nanopb runtime at ${NANOPB_RUNTIME_DIR} is incomplete.")
  endif()
  # The directory name is the pin: deriving the version from it rather than
  # comparing against a separately recorded value removes one way for the tree
  # to disagree with itself.
  get_filename_component(_nanopb_runtime_name "${NANOPB_RUNTIME_DIR}" NAME)
  string(REGEX REPLACE "^nanopb-" "" NANOPB_VERSION "${_nanopb_runtime_name}")
  message(STATUS "Nanopb runtime: ${NANOPB_RUNTIME_DIR} (version ${NANOPB_VERSION})")

  # The runtime states its own version. A disagreement here means the directory
  # was renamed without replacing its contents, or the reverse.
  file(READ "${NANOPB_RUNTIME_DIR}/pb.h" _nanopb_pb_h)
  string(REGEX MATCH "NANOPB_VERSION[ \t]+\"nanopb-([0-9][0-9.]*)\"" _ "${_nanopb_pb_h}")
  set(_nanopb_runtime_version "${CMAKE_MATCH_1}")
  unset(_nanopb_pb_h)
  if("${_nanopb_runtime_version}" STREQUAL "")
    reproducibility_problem(
      "Could not read NANOPB_VERSION from ${NANOPB_RUNTIME_DIR}/pb.h.")
  elseif(NOT "${_nanopb_runtime_version}" STREQUAL "${NANOPB_VERSION}")
    reproducibility_problem(
      "Vendored nanopb runtime disagrees with its directory name: "
      "${NANOPB_RUNTIME_DIR}/pb.h reports ${_nanopb_runtime_version}, but the "
      "directory is named for ${NANOPB_VERSION}. Replace the directory contents "
      "or rename the directory, whichever is wrong.")
  endif()

  # NANOPB_SRC_ROOT_FOLDER now supplies only the generator, which is a
  # build-time tool and stays outside the repository. The runtime is no longer
  # taken from it.
  set(NANOPB_SRC_ROOT_FOLDER "$ENV{NANOPB_ROOT}" CACHE PATH "Nanopb generator tree")
  if(NOT NANOPB_SRC_ROOT_FOLDER)
    set(NANOPB_SRC_ROOT_FOLDER "${CMAKE_SOURCE_DIR}/nanopb" CACHE PATH "Nanopb generator tree" FORCE)
  endif()
  # Missing here is not fatal: generated sources are committed, so an ordinary
  # build needs no generator at all. Whether one is actually required is decided
  # per proto variant, once the recorded inputs have been compared -- see
  # add_nanopb_target. Only REGENERATE_SOURCES=ON demands it unconditionally.
  if(NOT EXISTS "${NANOPB_SRC_ROOT_FOLDER}/generator"
     AND NOT EXISTS "${NANOPB_SRC_ROOT_FOLDER}/generator-bin")
    if(REGENERATE_SOURCES STREQUAL "ON")
      message(FATAL_ERROR
        "REGENERATE_SOURCES=ON but the nanopb generator was not found. Set "
        "NANOPB_ROOT, or place a nanopb distribution at "
        "${CMAKE_SOURCE_DIR}/nanopb. Only the generator is taken from there; "
        "the runtime is vendored at ${NANOPB_RUNTIME_DIR}.")
    endif()
    message(STATUS
      "No nanopb generator under ${NANOPB_SRC_ROOT_FOLDER}; building the "
      "committed generated sources. Set NANOPB_ROOT to regenerate them.")
    set(NANOPB_GENERATOR_EXECUTABLE "" CACHE FILEPATH "Nanopb generator executable" FORCE)
  endif()

  set(NANOPB_GENERATOR_EXECUTABLE "" CACHE FILEPATH "Nanopb generator executable")
  if(NANOPB_GENERATOR_EXECUTABLE AND EXISTS "${NANOPB_GENERATOR_EXECUTABLE}")
    # Use the cache value explicitly provided by the user or a previous valid configure.
  else()
    # A binary distribution ships the generator in generator-bin, a source tree
    # in generator. Search only under NANOPB_SRC_ROOT_FOLDER first: falling
    # straight through to PATH would silently use a generator unrelated to the
    # tree that was pointed at, which is indistinguishable from it having
    # worked.
    unset(_nanopb_generator_executable CACHE)
    find_program(
      _nanopb_generator_executable
      NAMES nanopb_generator nanopb_generator.bat
      HINTS "${NANOPB_SRC_ROOT_FOLDER}/generator-bin"
            "${NANOPB_SRC_ROOT_FOLDER}/generator"
      NO_DEFAULT_PATH
      DOC "Nanopb generator executable")
    if(NOT _nanopb_generator_executable)
      find_program(
        _nanopb_generator_executable
        NAMES nanopb_generator nanopb_generator.bat
        DOC "Nanopb generator executable")
      if(_nanopb_generator_executable)
        message(WARNING
          "No nanopb generator under ${NANOPB_SRC_ROOT_FOLDER} (looked in "
          "generator-bin and generator); using "
          "${_nanopb_generator_executable} from PATH instead. NANOPB_ROOT is "
          "not determining which generator this build uses.")
      endif()
    endif()
    if(_nanopb_generator_executable)
      set(
        NANOPB_GENERATOR_EXECUTABLE
        "${_nanopb_generator_executable}"
        CACHE FILEPATH
        "Nanopb generator executable"
        FORCE)
    endif()
  endif()
  if(NOT NANOPB_GENERATOR_EXECUTABLE AND REGENERATE_SOURCES STREQUAL "ON")
    message(FATAL_ERROR
      "REGENERATE_SOURCES=ON but nanopb_generator was not found. Set "
      "NANOPB_GENERATOR_EXECUTABLE, or set NANOPB_ROOT to a nanopb source tree "
      "with generator-bin.")
  endif()

  # The generator stamps its version into every .pb.h it writes, so a
  # generator that disagrees with the vendored runtime produces sources built
  # against a runtime they were not generated for. PB_PROTO_HEADER_VERSION only
  # catches that across major versions.
  if(NANOPB_GENERATOR_EXECUTABLE)
    execute_process(
      COMMAND "${NANOPB_GENERATOR_EXECUTABLE}" --version
      OUTPUT_VARIABLE _nanopb_generator_version_out
      ERROR_VARIABLE _nanopb_generator_version_err
      RESULT_VARIABLE _nanopb_generator_version_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "nanopb-([0-9][0-9.]*)"
           _ "${_nanopb_generator_version_out}${_nanopb_generator_version_err}")
    set(_nanopb_generator_version "${CMAKE_MATCH_1}")
    if(NOT _nanopb_generator_version_result EQUAL 0
       OR "${_nanopb_generator_version}" STREQUAL "")
      # Never fatal: a generator that cannot report its version is a gap in the
      # evidence, not proof of a mismatch, and failing here would block a build
      # over a tool's command line rather than over the tree's contents.
      # Carry the tool's own last error into the message. A source tree needs
      # its nanopb_pb2.py generated and the python protobuf packages installed
      # before the generator will run at all, and saying so here saves the reader
      # from rediscovering it.
      set(_nanopb_generator_why "")
      if(NOT "${_nanopb_generator_version_err}" STREQUAL "")
        string(REGEX REPLACE "\r?\n" ";" _nanopb_err_lines
               "${_nanopb_generator_version_err}")
        list(FILTER _nanopb_err_lines EXCLUDE REGEX "^[ \t]*$")
        if(_nanopb_err_lines)
          list(GET _nanopb_err_lines -1 _nanopb_generator_why)
          set(_nanopb_generator_why " It reported: ${_nanopb_generator_why}")
        endif()
      endif()
      message(WARNING
        "Could not determine the nanopb generator version from "
        "${NANOPB_GENERATOR_EXECUTABLE}; generated sources are unverified "
        "against the vendored runtime ${NANOPB_VERSION}.${_nanopb_generator_why}")
    elseif(NOT "${_nanopb_generator_version}" STREQUAL "${NANOPB_VERSION}")
      reproducibility_problem(
        "The nanopb generator at ${NANOPB_GENERATOR_EXECUTABLE} is version "
        "${_nanopb_generator_version} but the vendored runtime is "
        "${NANOPB_VERSION}. Generated .pb.c and .pb.h would be built against a "
        "runtime they were not generated for. Point NANOPB_ROOT or "
        "NANOPB_GENERATOR_EXECUTABLE at a ${NANOPB_VERSION} distribution, or "
        "vendor the runtime matching the generator.")
    else()
      message(STATUS
        "Nanopb generator: ${_nanopb_generator_version} matches runtime "
        "(${NANOPB_GENERATOR_EXECUTABLE})")
    endif()
  endif()

  get_filename_component(
    NANOPB_GENERATOR_SOURCE_DIR "${NANOPB_GENERATOR_EXECUTABLE}" DIRECTORY)
  if(EXISTS "${NANOPB_SRC_ROOT_FOLDER}/extra")
    list(APPEND CMAKE_MODULE_PATH "${NANOPB_SRC_ROOT_FOLDER}/extra/")
  endif()
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
      if(_ultralight_git_tag MATCHES "^v[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
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
# vX.Y and vX.Y.Z are both accepted; the workflow in .github/workflows/release.yml
# triggers on either. A tag with no patch component yields patch 0 so that
# CPACK_PACKAGE_VERSION_PATCH is always defined.
string(
  REGEX MATCH
  "^([0-9]+)\\.([0-9]+)\\.?([0-9]*)$"
  _ultralight_package_version_match
  "${ULTRALIGHT_PACKAGE_VERSION}")
set(ULTRALIGHT_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(ULTRALIGHT_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
if("${CMAKE_MATCH_3}" STREQUAL "")
  set(ULTRALIGHT_PACKAGE_VERSION_PATCH "0")
else()
  set(ULTRALIGHT_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_3}")
endif()
message(STATUS "UltralightTags package tag: ${ULTRALIGHT_PACKAGE_GIT_TAG}")

set(CPACK_PACKAGE_NAME "UltralightTags")
set(CPACK_PACKAGE_VENDOR "cs.iu.edu")
set(CPACK_PACKAGE_VERSION "${ULTRALIGHT_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${ULTRALIGHT_PACKAGE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${ULTRALIGHT_PACKAGE_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${ULTRALIGHT_PACKAGE_VERSION_PATCH}")
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
  # An ad-hoc identity is handled entirely by the install rules in
  # cmake/DeployQt.cmake; handing it to CPack as well would have the DragNDrop
  # generator re-sign the bundle inside the DMG with its own flags.
  if(MACOS_SIGN_APPS AND MACOS_CODE_SIGN_IDENTITY AND NOT MACOS_CODE_SIGN_IDENTITY STREQUAL "-")
    set(CPACK_BUNDLE_APPLE_CERT_APP "${MACOS_CODE_SIGN_IDENTITY}")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${MACOS_CODE_SIGN_IDENTITY}")
  endif()
endif()

include(CPack)
