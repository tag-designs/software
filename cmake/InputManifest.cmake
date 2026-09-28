# Input manifest for committed generated sources.
#
# The generated .pb.c, .pb.h and default_config.c files are committed so that an
# ordinary firmware build needs no code generator.  That only helps if a stale
# committed file can be told from a fresh one, which is what this manifest is
# for: it records the SHA-256 of every input the generators consume, plus the
# tool versions, so a checkout can decide whether regeneration is needed without
# running -- or even having -- the generators.
#
# The same function produces the manifest at configure time (for comparison) and
# at build time (for writing), so the two cannot disagree about the format.
#
# Timestamps are deliberately not used: git does not preserve mtimes, so a fresh
# clone would look stale and a checkout could look fresh when it is not.

# EXTRA_ROOT/EXTRA_LABEL name inputs that live outside the repository -- the
# ChibiOS board templates, which are a submodule that CHIBIOS_DIR may point
# anywhere. Recording those by relative path would put "../../.." in the
# manifest and make it differ between machines for no real reason, so they are
# recorded under a fixed label instead. Their contents are still hashed, so a
# submodule bump still shows up as stale.

function(input_manifest_text out_var)
  cmake_parse_arguments(IM "" "ROOT;TOOLS;EXTRA_ROOT;EXTRA_LABEL" "INPUTS" ${ARGN})
  if(NOT IM_ROOT)
    message(FATAL_ERROR "input_manifest_text: ROOT is required")
  endif()
  if(IM_EXTRA_ROOT AND NOT IM_EXTRA_LABEL)
    message(FATAL_ERROR "input_manifest_text: EXTRA_ROOT requires EXTRA_LABEL")
  endif()

  set(_text "# Generated-source inputs. Regenerate with -DREGENERATE_SOURCES=ON.\n")
  set(_text "${_text}tools ${IM_TOOLS}\n")

  # Sort so the manifest does not depend on the order inputs were listed in.
  set(_inputs ${IM_INPUTS})
  list(SORT _inputs)
  foreach(_input IN LISTS _inputs)
    if(NOT EXISTS "${_input}")
      message(FATAL_ERROR "input_manifest_text: missing input ${_input}")
    endif()
    file(SHA256 "${_input}" _hash)
    set(_rel "")
    if(IM_EXTRA_ROOT)
      file(RELATIVE_PATH _under_extra "${IM_EXTRA_ROOT}" "${_input}")
      if(NOT _under_extra MATCHES "^\\.\\.")
        set(_rel "${IM_EXTRA_LABEL}/${_under_extra}")
      endif()
    endif()
    if("${_rel}" STREQUAL "")
      file(RELATIVE_PATH _rel "${IM_ROOT}" "${_input}")
    endif()
    set(_text "${_text}${_hash}  ${_rel}\n")
  endforeach()

  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

# Script mode:
#   cmake -DOUTPUT=... -DROOT=... -DTOOLS=... -DINPUTS_FILE=... -P this
#
# The input list arrives in a file rather than on the command line: a CMake list
# passed through -D would expand into separate arguments, and the paths are long
# enough to risk the command-line limit on Windows.
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED OUTPUT)
  if(NOT DEFINED INPUTS_FILE)
    message(FATAL_ERROR "InputManifest.cmake: INPUTS_FILE is required")
  endif()
  file(STRINGS "${INPUTS_FILE}" _inputs)
  set(_extra_args "")
  if(DEFINED EXTRA_ROOT)
    list(APPEND _extra_args EXTRA_ROOT "${EXTRA_ROOT}" EXTRA_LABEL "${EXTRA_LABEL}")
  endif()
  input_manifest_text(_manifest ROOT "${ROOT}" TOOLS "${TOOLS}"
                      ${_extra_args} INPUTS ${_inputs})
  file(WRITE "${OUTPUT}" "${_manifest}")
endif()
