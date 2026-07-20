cmake_minimum_required(VERSION 3.10)

set(BOARD_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/tools")

# function to generate board.[c|h|mk] files with fmpp from configuration

function(generate_board_files name)
  set(BOARD_FILES board.h board.c board.mk)

  add_custom_command(
    OUTPUT ${BOARD_FILES}
    COMMAND ${FMPP} --freemarker-links "{lib : ${CHIBIOS_DIR}/tools/ftl/libs}"
            -C board.fmpp -v -O ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cfg
    DEPENDS cfg/board.c.ftl cfg/board.chcfg cfg/board.fmpp cfg/board.h.ftl
    #COMMENT "Building ${BOARD_FILES}"
    #COMMENT
    #  "${FMPP} --freemarker-links \"{lib : ${CHIBIOS_DIR}/tools/ftl/libs}\" -C board.fmpp -v -O ${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_custom_target(${name} DEPENDS ${BOARD_FILES})
endfunction()

# Generates board.[c|h|mk] from a ChibiOS processor-family template plus a
# small JSON customization file. This is the preferred path for new boards:
# keep the processor-owned XML and FTL templates in ChibiOS, then store only
# project-specific changes in embedded/boards/<board>/cfg/*.json.
#
# Arguments:
#   name           CMake target name to create, e.g. board-bitprestag.
#   PROCESSOR      ChibiOS processor template family, e.g. stm32l4xx.
#   BOARD_TYPE     Output board directory and board.mk include name.
#                  Firmware makefiles include this as
#                  $(BOARDDIR)/<BOARD_TYPE>/board.mk.
#                  The JSON board_id should normally match this value because
#                  stock ChibiOS board.mk.ftl uses board_id for BOARDSRC and
#                  BOARDINC.
#   CUSTOMIZATIONS JSON file, relative to the board directory, consumed by
#                  tools/generate_board_chcfg.py.
#
# The two-step build mirrors the old static-board flow:
#   1. generate_board_chcfg.py creates a build-tree cfg/board.chcfg from
#      ChibiOS/tools/ftl/xml/<processor>board.xml.
#   2. fmpp renders ChibiOS' matching board.c/h/mk templates.
function(generate_configured_board_files name)
  set(one_value_args PROCESSOR BOARD_TYPE CUSTOMIZATIONS)
  cmake_parse_arguments(GCB "" "${one_value_args}" "" ${ARGN})

  if(NOT GCB_PROCESSOR)
    message(FATAL_ERROR "generate_configured_board_files requires PROCESSOR")
  endif()
  if(NOT GCB_BOARD_TYPE)
    message(FATAL_ERROR "generate_configured_board_files requires BOARD_TYPE")
  endif()
  if(NOT GCB_CUSTOMIZATIONS)
    message(FATAL_ERROR "generate_configured_board_files requires CUSTOMIZATIONS")
  endif()

  find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)

  # ChibiOS uses stm32l4board.xml for the stm32l4xx template family, while the
  # FTL template directory keeps the full family name stm32l4xx.
  string(TOLOWER "${GCB_PROCESSOR}" _processor)
  string(REGEX REPLACE "xx$" "" _processor_xml "${_processor}")

  set(_template_xml "${CHIBIOS_DIR}/tools/ftl/xml/${_processor_xml}board.xml")
  set(_template_dir "${CHIBIOS_DIR}/tools/ftl/processors/boards/${_processor}/templates")
  set(_customizations "${CMAKE_CURRENT_SOURCE_DIR}/${GCB_CUSTOMIZATIONS}")
  set(_generated_cfg_dir "${CMAKE_CURRENT_BINARY_DIR}/cfg")
  set(_generated_board_chcfg "${_generated_cfg_dir}/board.chcfg")
  set(_generated_board_fmpp "${_generated_cfg_dir}/board.fmpp")

  # Firmware builds pass BOARDDIR=<build>/embedded/boards, so generated boards
  # must land beside the normal board build directories, not inside this
  # sample/configuration directory's private build directory.
  set(_board_output_dir "${CMAKE_CURRENT_BINARY_DIR}/../${GCB_BOARD_TYPE}")
  set(_generated_board_standby "${_board_output_dir}/board_standby.h")

  if(NOT EXISTS "${_template_xml}")
    message(FATAL_ERROR "Missing ChibiOS board XML template: ${_template_xml}")
  endif()
  if(NOT EXISTS "${_template_dir}/board.c.ftl")
    message(FATAL_ERROR "Missing ChibiOS board template directory: ${_template_dir}")
  endif()

  file(MAKE_DIRECTORY "${_generated_cfg_dir}")

  # Keep board.fmpp generated in the build tree because its sourceRoot,
  # outputRoot, and dataRoot are all build- and ChibiOS-location dependent.
  set(BOARD_FMPP_SOURCE_ROOT "${_template_dir}")
  set(BOARD_FMPP_OUTPUT_ROOT "${_board_output_dir}")
  set(BOARD_FMPP_DATA_ROOT "${_generated_cfg_dir}")
  configure_file(
    "${BOARD_TOOLS_DIR}/board.fmpp.in"
    "${_generated_board_fmpp}"
    @ONLY)

  add_custom_command(
    OUTPUT
      "${_generated_board_chcfg}"
      "${_generated_board_standby}"
    COMMAND ${PYTHON_EXECUTABLE}
            "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
            --template "${_template_xml}"
            --customizations "${_customizations}"
            --output "${_generated_board_chcfg}"
            --standby-header "${_generated_board_standby}"
    DEPENDS
      "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
      "${_customizations}"
      "${_template_xml}"
    VERBATIM
    COMMENT "Generating ${GCB_BOARD_TYPE}/board.chcfg")

  set(_fmpp_board_files
    "${_board_output_dir}/board.h"
    "${_board_output_dir}/board.c"
    "${_board_output_dir}/board.mk")

  set(_board_files
    ${_fmpp_board_files}
    "${_generated_board_standby}")

  add_custom_command(
    OUTPUT ${_fmpp_board_files}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_board_output_dir}"
    COMMAND ${FMPP} --freemarker-links "{lib : ${CHIBIOS_DIR}/tools/ftl/libs}"
            -C "${_generated_board_fmpp}" -v -O "${_board_output_dir}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS
      "${_generated_board_chcfg}"
      "${_generated_board_standby}"
      "${_generated_board_fmpp}"
      "${_template_dir}/board.c.ftl"
      "${_template_dir}/board.h.ftl"
      "${_template_dir}/board.mk.ftl"
    VERBATIM
    COMMENT "Building generated board files for ${GCB_BOARD_TYPE}")

  add_custom_target(${name} DEPENDS ${_board_files})
endfunction()

# bases

#add_subdirectory(bittag-base-v5a)
#add_subdirectory(bittag-base-v5)
#add_subdirectory(bittag-base-jlcpcb-v2)
add_subdirectory(bittag-base-jlcpcb-v3)
add_subdirectory(tag-breakout-base-jlcpcb32-v1)
add_subdirectory(tag-breakout-base-l432-v1)
add_subdirectory(tag-breakout-base-l432-u375-1v8)
add_subdirectory(bittag-base-v7)
add_subdirectory(tag-base-c071-v1)


# tags

#add_subdirectory(BitTagv5)
add_subdirectory(BitTagv6)
add_subdirectory(TagSteval)
add_subdirectory(PresTagv3)
#add_subdirectory(PresTagBreakout)
#add_subdirectory(BitTagNG-proto)
add_subdirectory(BitTagNG)
add_subdirectory(BitPresTagv1)
add_subdirectory(CompassTagv1)
add_subdirectory(IMUTagv1)
add_subdirectory(IMUTagNandv1)
add_subdirectory(IMUTagU375)
