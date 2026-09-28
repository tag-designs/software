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

function(input_manifest_text out_var)
  cmake_parse_arguments(IM "" "ROOT;TOOLS" "INPUTS" ${ARGN})
  if(NOT IM_ROOT)
    message(FATAL_ERROR "input_manifest_text: ROOT is required")
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
    file(RELATIVE_PATH _rel "${IM_ROOT}" "${_input}")
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
  input_manifest_text(_manifest ROOT "${ROOT}" TOOLS "${TOOLS}" INPUTS ${_inputs})
  file(WRITE "${OUTPUT}" "${_manifest}")
endif()
