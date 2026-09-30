# Check that committed generated sources match their recorded inputs.
#
#   cmake -DSOURCE_DIR=<repo root> -P cmake/CheckGeneratedSourcesFresh.cmake
#
# This is the CI form of what add_nanopb_target decides at configure time, and
# it reuses the same InputManifest.cmake function, so the two cannot disagree
# about what a manifest should contain.
#
# It deliberately needs nothing but cmake: no ARM toolchain, no fmpp, no nanopb
# generator, no submodules. A check that required the generator to prove the
# generator was not needed would be self-defeating, and one that required the
# whole embedded configure would fail for reasons unrelated to freshness.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/InputManifest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/GeneratedSourceInputs.cmake")

# The pin is the vendored directory's name, derived exactly as the build derives
# it, so a runtime bump shows up here as a stale manifest rather than silently.

file(GLOB _runtime_dirs "${SOURCE_DIR}/embedded/thirdparty/nanopb-*")
list(LENGTH _runtime_dirs _runtime_count)
if(NOT _runtime_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one vendored nanopb runtime under "
    "embedded/thirdparty/nanopb-*, found ${_runtime_count}.")
endif()
list(GET _runtime_dirs 0 _runtime_dir)
get_filename_component(_runtime_name "${_runtime_dir}" NAME)
string(REGEX REPLACE "^nanopb-" "" _nanopb_version "${_runtime_name}")

# The .proto set, read from proto/CMakeLists.txt rather than hardcoded: adding a
# .proto there must make the manifests stale, and a list duplicated here would
# quietly fail to notice.

file(READ "${SOURCE_DIR}/proto/CMakeLists.txt" _proto_cmake)
string(REGEX MATCH "target_sources\\(tag-proto-sources INTERFACE([^)]*)\\)"
       _ "${_proto_cmake}")
set(_proto_block "${CMAKE_MATCH_1}")
if("${_proto_block}" STREQUAL "")
  message(FATAL_ERROR
    "Could not find the tag-proto-sources source list in "
    "proto/CMakeLists.txt; this check needs updating alongside it.")
endif()
string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.proto" _proto_names "${_proto_block}")
if(NOT _proto_names)
  message(FATAL_ERROR "No .proto files found in the tag-proto-sources list.")
endif()
set(_proto_sources "")
foreach(_name IN LISTS _proto_names)
  get_filename_component(_base "${_name}" NAME)
  list(APPEND _proto_sources "${SOURCE_DIR}/proto/${_base}")
endforeach()

file(GLOB _generated_dirs "${SOURCE_DIR}/embedded/proto-c/*/generated")
if(NOT _generated_dirs)
  message(FATAL_ERROR
    "No committed generated sources found under embedded/proto-c/*/generated.")
endif()

set(_stale "")
set(_checked 0)

foreach(_generated_dir IN LISTS _generated_dirs)
  get_filename_component(_variant_dir "${_generated_dir}" DIRECTORY)
  get_filename_component(_variant "${_variant_dir}" NAME)

  # Shared with add_nanopb_target, which writes these manifests, so the two
  # cannot disagree about what an input is.
  proto_c_manifest_inputs(_inputs
                          SOURCE_DIR "${SOURCE_DIR}"
                          VARIANT_DIR "${_variant_dir}"
                          PROTO_SOURCES ${_proto_sources})

  # Check the inputs exist before hashing them. A .proto added to the source
  # list without a matching per-variant override lands here, and saying which
  # file is missing is more use than letting the hashing report it.

  set(_absent "")
  foreach(_input IN LISTS _inputs)
    if(NOT EXISTS "${_input}")
      file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_input}")
      list(APPEND _absent "${_rel}")
    endif()
  endforeach()
  if(_absent)
    list(JOIN _absent ", " _absent_text)
    list(APPEND _stale "${_variant}: missing input ${_absent_text}")
    continue()
  endif()

  input_manifest_text(_expected
                      ROOT "${SOURCE_DIR}"
                      TOOLS "nanopb ${_nanopb_version}"
                      INPUTS ${_inputs})

  set(_manifest "${_generated_dir}/inputs.sha256")
  if(NOT EXISTS "${_manifest}")
    list(APPEND _stale "${_variant}: no inputs.sha256")
    continue()
  endif()
  file(READ "${_manifest}" _committed)

  if(NOT _expected STREQUAL _committed)
    list(APPEND _stale "${_variant}: recorded inputs do not match the tree")
  endif()

  # A manifest can agree while an output is simply absent.
  foreach(_proto IN LISTS _proto_sources)
    get_filename_component(_stem "${_proto}" NAME_WE)
    foreach(_ext c h)
      if(NOT EXISTS "${_generated_dir}/${_stem}.pb.${_ext}")
        list(APPEND _stale "${_variant}: missing ${_stem}.pb.${_ext}")
      endif()
    endforeach()
  endforeach()
  if(NOT EXISTS "${_generated_dir}/default_config.c")
    list(APPEND _stale "${_variant}: missing default_config.c")
  endif()

  math(EXPR _checked "${_checked} + 1")
endforeach()

# --- boards ------------------------------------------------------------------
#
# Board files are generated from per-board customizations plus ChibiOS' own
# templates, so a submodule bump makes them stale as surely as an edit does.
# That is the point: the templates are an input like any other.
#
# CHIBIOS_DIR defaults to the in-repo submodule. If it is not checked out there
# is nothing to hash, and the board half is skipped with a message rather than
# failing -- a check that demanded submodules would be a different check.

set(_chibios "${SOURCE_DIR}/ChibiOS")
set(_boards_checked 0)

if(NOT EXISTS "${_chibios}/tools/ftl/libs")
  # Skipping is right for a developer who has not initialized the submodule,
  # and wrong for CI, where a skip is indistinguishable from a pass. REQUIRE_
  # CHIBIOS=ON makes the absence an error so the job cannot quietly check
  # nothing.
  if(REQUIRE_CHIBIOS)
    message(FATAL_ERROR
      "REQUIRE_CHIBIOS=ON but ChibiOS is not checked out at ${_chibios}, so the "
      "board files cannot be checked. Check out the submodule.")
  endif()
  message(STATUS
    "ChibiOS is not checked out; skipping the board file check. Run "
    "`git submodule update --init ChibiOS` to include it.")
else()
  file(GLOB _chibios_libs "${_chibios}/tools/ftl/libs/*")
  file(GLOB _board_generated_dirs "${SOURCE_DIR}/embedded/boards/*/generated")

  foreach(_generated_dir IN LISTS _board_generated_dirs)
    get_filename_component(_board_dir "${_generated_dir}" DIRECTORY)
    get_filename_component(_board "${_board_dir}" NAME)

    # The processor family is named in the board's own CMakeLists.txt, which is
    # what picks the templates; reading it here keeps the two from disagreeing.
    file(READ "${_board_dir}/CMakeLists.txt" _board_cmake)
    string(REGEX MATCH "PROCESSOR[ \t\r\n]+([A-Za-z0-9_]+)" _ "${_board_cmake}")
    set(_processor "${CMAKE_MATCH_1}")
    if("${_processor}" STREQUAL "")
      list(APPEND _stale "${_board}: could not read PROCESSOR from its CMakeLists.txt")
      continue()
    endif()
    string(TOLOWER "${_processor}" _processor)
    string(REGEX REPLACE "xx$" "" _processor_xml "${_processor}")
    set(_template_dir "${_chibios}/tools/ftl/processors/boards/${_processor}/templates")

    set(_board_inputs
        "${_board_dir}/cfg/board-customizations.json"
        "${SOURCE_DIR}/embedded/boards/tools/generate_board_chcfg.py"
        "${SOURCE_DIR}/embedded/boards/tools/board.fmpp.in"
        "${_chibios}/tools/ftl/xml/${_processor_xml}board.xml"
        "${_template_dir}/board.c.ftl"
        "${_template_dir}/board.h.ftl"
        "${_template_dir}/board.mk.ftl")
    list(APPEND _board_inputs ${_chibios_libs})

    set(_absent "")
    foreach(_input IN LISTS _board_inputs)
      if(NOT EXISTS "${_input}")
        file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_input}")
        list(APPEND _absent "${_rel}")
      endif()
    endforeach()
    if(_absent)
      list(JOIN _absent ", " _absent_text)
      list(APPEND _stale "${_board}: missing input ${_absent_text}")
      continue()
    endif()

    input_manifest_text(_expected
                        ROOT "${SOURCE_DIR}"
                        TOOLS "chibios-templates ${_processor}"
                        EXTRA_ROOT "${_chibios}"
                        EXTRA_LABEL "<chibios>"
                        INPUTS ${_board_inputs})

    set(_manifest "${_generated_dir}/inputs.sha256")
    if(NOT EXISTS "${_manifest}")
      list(APPEND _stale "${_board}: no inputs.sha256")
      continue()
    endif()
    file(READ "${_manifest}" _committed)
    if(NOT _expected STREQUAL _committed)
      list(APPEND _stale "${_board}: recorded inputs do not match the tree")
    endif()

    foreach(_f board.h board.c board.mk board_standby.h)
      if(NOT EXISTS "${_generated_dir}/${_f}")
        list(APPEND _stale "${_board}: missing ${_f}")
      endif()
    endforeach()

    math(EXPR _boards_checked "${_boards_checked} + 1")
  endforeach()
endif()

if(_stale)
  list(JOIN _stale "\n  " _stale_text)
  message(FATAL_ERROR
    "Committed generated sources are out of date:\n  ${_stale_text}\n\n"
    "Regenerate them with `cmake -DREGENERATE_SOURCES=ON` followed by "
    "`cmake --build <dir> --target distributed_proto_sources` and a build of "
    "the affected board targets, and commit the result.")
endif()

message(STATUS
  "Generated sources are up to date: ${_checked} proto variant(s) "
  "(nanopb ${_nanopb_version}), ${_boards_checked} board(s).")
