# Which files a generated/ directory's manifest hashes.
#
# This exists because the list was written out twice -- once in
# add_nanopb_target, which writes the manifests, and once in
# CheckGeneratedSourcesFresh.cmake, which recomputes them -- and nothing made
# the two agree. They disagreed the moment config-gen moved from C++ to Python:
# the build hashed config-gen.py, the check still hashed config-gen.cc, and
# every committed inputs.sha256 would have looked stale on every pull request.
#
# The same reasoning already applies elsewhere in this machinery. InputManifest
# produces the manifest text for both writer and checker so they cannot disagree
# about the format; the check reads the .proto list out of proto/CMakeLists.txt
# rather than repeating it; each board's PROCESSOR is read from its own
# CMakeLists.txt. This closes the one remaining place where a list was kept in
# two hands.
#
# Order does not matter: input_manifest_text sorts before hashing.

# The inputs behind one proto-c variant's generated sources.
#
#   SOURCE_DIR    the repository root
#   VARIANT_DIR   the variant's source directory, e.g. embedded/proto-c/bittag-proto-c
#   PROTO_SOURCES absolute paths of the .proto files in tag-proto-sources
function(proto_c_manifest_inputs out_var)
  cmake_parse_arguments(PI "" "SOURCE_DIR;VARIANT_DIR" "PROTO_SOURCES" ${ARGN})
  foreach(_required SOURCE_DIR VARIANT_DIR PROTO_SOURCES)
    if(NOT PI_${_required})
      message(FATAL_ERROR "proto_c_manifest_inputs: ${_required} is required")
    endif()
  endforeach()

  set(_proto_c_dir "${PI_SOURCE_DIR}/embedded/proto-c")
  set(_inputs
      "${PI_VARIANT_DIR}/default-config.json"
      "${_proto_c_dir}/config-gen.py"
      "${PI_SOURCE_DIR}/cmake/CombineFiles.cmake")
  list(APPEND _inputs ${PI_PROTO_SOURCES})

  foreach(_proto IN LISTS PI_PROTO_SOURCES)
    get_filename_component(_stem "${_proto}" NAME_WE)
    list(APPEND _inputs
         "${_proto_c_dir}/default-options/${_stem}.options"
         "${PI_VARIANT_DIR}/${_stem}.override.options")
  endforeach()

  set(${out_var} "${_inputs}" PARENT_SCOPE)
endfunction()

# Where a board's ChibiOS templates live, given its processor family.
#
# The rule is small -- lowercase, and drop a trailing "xx" for the XML file --
# but the build renders from these paths and the manifest hashes them, so the
# two deriving it separately would mean a board could be rendered from one
# directory and hashed against another.
function(board_template_paths dir_var xml_var)
  cmake_parse_arguments(BT "" "CHIBIOS_DIR;PROCESSOR" "" ${ARGN})
  foreach(_required CHIBIOS_DIR PROCESSOR)
    if(NOT BT_${_required})
      message(FATAL_ERROR "board_template_paths: ${_required} is required")
    endif()
  endforeach()

  string(TOLOWER "${BT_PROCESSOR}" _processor)
  string(REGEX REPLACE "xx$" "" _processor_xml "${_processor}")
  set(${dir_var}
      "${BT_CHIBIOS_DIR}/tools/ftl/processors/boards/${_processor}/templates"
      PARENT_SCOPE)
  set(${xml_var}
      "${BT_CHIBIOS_DIR}/tools/ftl/xml/${_processor_xml}board.xml"
      PARENT_SCOPE)
endfunction()

# The inputs behind one board's generated files.
#
#   SOURCE_DIR     the repository root
#   BOARD_DIR      the board's source directory, e.g. embedded/boards/BitTagv6
#   CHIBIOS_DIR    the ChibiOS tree supplying the templates
#   PROCESSOR      the family named in the board's own CMakeLists.txt, e.g. STM32L4xx
#   CUSTOMIZATIONS the customizations file, relative to BOARD_DIR
#
# PROCESSOR and CUSTOMIZATIONS are what the board's generate_configured_board_files
# call passes. The freshness check reads both out of that call rather than
# assuming them: it already did so for PROCESSOR, and assuming the
# customizations path would mean hashing a file the build never read the first
# time a board put it somewhere else.
#
# The derivation from PROCESSOR to template paths lives here too. It is small
# -- lowercase, then drop a trailing "xx" for the XML -- but it was written out
# twice, and a ChibiOS layout change would have had to be caught in both.
function(board_manifest_inputs out_var)
  cmake_parse_arguments(BI ""
                        "SOURCE_DIR;BOARD_DIR;CHIBIOS_DIR;PROCESSOR;CUSTOMIZATIONS"
                        "" ${ARGN})
  foreach(_required SOURCE_DIR BOARD_DIR CHIBIOS_DIR PROCESSOR CUSTOMIZATIONS)
    if(NOT BI_${_required})
      message(FATAL_ERROR "board_manifest_inputs: ${_required} is required")
    endif()
  endforeach()

  board_template_paths(_template_dir _template_xml
                       CHIBIOS_DIR "${BI_CHIBIOS_DIR}"
                       PROCESSOR "${BI_PROCESSOR}")
  set(_board_tools "${BI_SOURCE_DIR}/embedded/boards/tools")

  set(_inputs
      "${BI_BOARD_DIR}/${BI_CUSTOMIZATIONS}"
      "${_board_tools}/generate_board_chcfg.py"
      "${_board_tools}/board.fmpp.in"
      "${_template_xml}"
      "${_template_dir}/board.c.ftl"
      "${_template_dir}/board.h.ftl"
      "${_template_dir}/board.mk.ftl")

  # Everything fmpp loads from the ChibiOS library directory is an input too.
  file(GLOB _chibios_libs "${BI_CHIBIOS_DIR}/tools/ftl/libs/*")
  list(APPEND _inputs ${_chibios_libs})

  set(${out_var} "${_inputs}" PARENT_SCOPE)
endfunction()
