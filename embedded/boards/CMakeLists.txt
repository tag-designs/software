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

  find_program(PYTHON_EXECUTABLE NAMES python3 python)

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

  set(_fmpp_board_files
    "${_board_output_dir}/board.h"
    "${_board_output_dir}/board.c"
    "${_board_output_dir}/board.mk")

  set(_board_files
    ${_fmpp_board_files}
    "${_generated_board_standby}")

  # Committed generated board files.
  #
  # A board declares that it commits them by having a generated/ directory, the
  # same way a proto-c variant does.  Boards used only by prototype tags keep
  # generating at build time; the five behind distributed tags do not, so an
  # ordinary firmware build needs neither fmpp nor a Java runtime.
  #
  # Consumption is unchanged either way: the firmware makefiles reach the board
  # through BOARDDIR, which points into the build tree, so the committed files
  # are copied there rather than the build being pointed at the source tree.
  # That keeps one path for both cases and leaves board.mk's own
  # $(BOARDDIR)/<board>/... references correct.

  set(_committed_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated")
  set(_manifest "${_committed_dir}/inputs.sha256")
  if(IS_DIRECTORY "${_committed_dir}")
    set(_sources_committed TRUE)
    set_property(GLOBAL APPEND PROPERTY ULTRALIGHT_BOARD_COMMITTED "${GCB_BOARD_TYPE}")
  else()
    set(_sources_committed FALSE)
  endif()
  set_property(GLOBAL APPEND PROPERTY ULTRALIGHT_BOARD_ALL "${GCB_BOARD_TYPE}")

  # What the output depends on.  The ChibiOS templates are inputs as much as the
  # customizations are, so a submodule bump correctly makes every board stale.
  set(_chibios_lib_dir "${CHIBIOS_DIR}/tools/ftl/libs")
  file(GLOB _chibios_libs "${_chibios_lib_dir}/*")
  set(_manifest_inputs
      "${_customizations}"
      "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
      "${BOARD_TOOLS_DIR}/board.fmpp.in"
      "${_template_xml}"
      "${_template_dir}/board.c.ftl"
      "${_template_dir}/board.h.ftl"
      "${_template_dir}/board.mk.ftl")
  list(APPEND _manifest_inputs ${_chibios_libs})

  # fmpp's own version is deliberately not in this manifest.  It has to compare
  # equal across machines, and a tool version that is reported on one machine
  # and not another would make boards look stale for a reason unrelated to the
  # tree.  The per-image build manifest is where "what this machine had" belongs.
  input_manifest_text(_expected_manifest
                      ROOT "${CMAKE_SOURCE_DIR}"
                      TOOLS "chibios-templates ${GCB_PROCESSOR}"
                      EXTRA_ROOT "${CHIBIOS_DIR}"
                      EXTRA_LABEL "<chibios>"
                      INPUTS ${_manifest_inputs})

  set(_committed_manifest "")
  if(EXISTS "${_manifest}")
    file(READ "${_manifest}" _committed_manifest)
  endif()

  set(_committed_outputs_present ${_sources_committed})
  if(_sources_committed)
    foreach(_f board.h board.c board.mk board_standby.h)
      if(NOT EXISTS "${_committed_dir}/${_f}")
        set(_committed_outputs_present FALSE)
      endif()
    endforeach()
  endif()

  if(_expected_manifest STREQUAL _committed_manifest AND _committed_outputs_present)
    set(_up_to_date TRUE)
  else()
    set(_up_to_date FALSE)
  endif()

  set(_unbuildable FALSE)
  if(REGENERATE_SOURCES STREQUAL "ON")
    set(_regenerate TRUE)
  elseif(REGENERATE_SOURCES STREQUAL "OFF")
    set(_regenerate FALSE)
    # As for proto variants: a board that does not commit its files has nothing
    # to build under OFF, but that must not stop a release build from
    # configuring the whole tree and building only the distributed targets.
    if(NOT _sources_committed)
      set(_unbuildable TRUE)
    endif()
    if(NOT _up_to_date AND _sources_committed)
      reproducibility_problem(
        "Committed board files for ${GCB_BOARD_TYPE} are stale: the inputs "
        "recorded in ${_manifest} do not match the current customizations or "
        "ChibiOS templates. REGENERATE_SOURCES=OFF means they will be built "
        "as-is.")
    endif()
  elseif(_sources_committed AND _up_to_date)
    set(_regenerate FALSE)
  else()
    set(_regenerate TRUE)
  endif()

  if(_regenerate)
    # The generation rules exist only in this branch. Registering them
    # unconditionally would leave two custom commands claiming the same outputs
    # whenever the committed files are used instead, which CMake rejects at
    # generate time.
    #
    # This is also the only place that needs the generators, so it is where they
    # are demanded rather than at the top of the embedded tree.
    if(NOT FMPP)
      message(FATAL_ERROR
        "Board ${GCB_BOARD_TYPE} needs regenerating, which requires fmpp (and a "
        "Java runtime). Install fmpp, or commit its generated files so the "
        "build does not need it.")
    endif()
    if(NOT PYTHON_EXECUTABLE)
      message(FATAL_ERROR
        "Board ${GCB_BOARD_TYPE} needs regenerating, which requires python3.")
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

    if(_sources_committed)
      # Generate into the build tree as always, then copy back, so the
      # committed copy is exactly what a build produced.
      add_custom_command(
        OUTPUT "${_manifest}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_committed_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_board_files} "${_committed_dir}/"
        COMMAND ${CMAKE_COMMAND}
                "-DOUTPUT=${_manifest}"
                "-DROOT=${CMAKE_SOURCE_DIR}"
                "-DTOOLS=chibios-templates ${GCB_PROCESSOR}"
                "-DEXTRA_ROOT=${CHIBIOS_DIR}"
                "-DEXTRA_LABEL=<chibios>"
                "-DINPUTS_FILE=${CMAKE_CURRENT_BINARY_DIR}/manifest-inputs.txt"
                -P "${CMAKE_SOURCE_DIR}/cmake/InputManifest.cmake"
        DEPENDS ${_board_files} "${CMAKE_SOURCE_DIR}/cmake/InputManifest.cmake"
        COMMENT "Recording board inputs for ${GCB_BOARD_TYPE}"
        VERBATIM)
      string(REPLACE ";" "\n" _manifest_inputs_lines "${_manifest_inputs}")
      file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/manifest-inputs.txt"
           "${_manifest_inputs_lines}\n")
      add_custom_target(${name} DEPENDS ${_board_files} "${_manifest}")
    else()
      add_custom_target(${name} DEPENDS ${_board_files})
    endif()
  elseif(_unbuildable)
    add_custom_target(${name}
      COMMAND ${CMAKE_COMMAND} -E echo
              "Board ${GCB_BOARD_TYPE} does not commit its generated files, and REGENERATE_SOURCES=OFF forbids generating them. Configure with -DREGENERATE_SOURCES=AUTO to build it."
      COMMAND ${CMAKE_COMMAND} -E false
      VERBATIM)
  else()
    # Copy the committed files into the build tree, where BOARDDIR expects them.
    set(_installed_board_files "")
    foreach(_f board.h board.c board.mk board_standby.h)
      list(APPEND _installed_board_files "${_board_output_dir}/${_f}")
    endforeach()
    add_custom_command(
      OUTPUT ${_installed_board_files}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_board_output_dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_committed_dir}/board.h" "${_committed_dir}/board.c"
              "${_committed_dir}/board.mk" "${_committed_dir}/board_standby.h"
              "${_board_output_dir}/"
      DEPENDS
        "${_committed_dir}/board.h" "${_committed_dir}/board.c"
        "${_committed_dir}/board.mk" "${_committed_dir}/board_standby.h"
      COMMENT "Using committed board files for ${GCB_BOARD_TYPE}"
      VERBATIM)
    add_custom_target(${name} DEPENDS ${_installed_board_files})
  endif()
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
add_subdirectory(IMUTagNandv2)
add_subdirectory(IMUTagU375)
add_subdirectory(Stop1Test)
add_subdirectory(UIUCTag)
