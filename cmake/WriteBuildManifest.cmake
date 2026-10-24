# Write a build manifest beside a firmware image.
#
# Run as a script after the image is linked:
#
#   cmake -DNAME=... -DOUTPUT=... -DSOURCE_DIR=... -DARTIFACT_DIR=... \
#         -DNANOPB_RUNTIME_VERSION=... -DNANOPB_GENERATOR_VERSION=... \
#         -DARM_TOOLCHAIN=... -DARM_TOOLCHAIN_VERSION=... \
#         -DREPRODUCIBLE_BUILD=... -DREGENERATE_SOURCES=... \
#         -DSOURCES_COMMITTED=... [-DREQUIRE_IDENTITY=true] \
#         -P WriteBuildManifest.cmake
#
# Facts that are fixed at configure time arrive as arguments; the ones that can
# change between configuring and linking -- the commit, whether the tree is
# dirty, the submodule commit -- are read here, so the manifest describes the
# tree the image was actually built from rather than the tree it was configured
# from.
#
# The image hash is the point of the file. VERSION_HASH and SHAStr name a
# commit, and a commit does not identify an image: a -D leaves no trace in the
# git hash and uncommitted changes are invisible to rev-parse, so two materially
# different binaries can report the same SHA. The SHA-256 of the .bin is what a
# capture of internal flash can be keyed on with no metadata to trust.
#
# `tag_type` and `board` say which tag the image is for, so a programming tool
# can match a release image to the tag it finds attached. Both are read from the
# tag identity record compiled into the .bin (embedded/tags/common/core/inc/
# tag_identity.h) -- the same bytes tag-info reports as TagInfo.tag_type and
# TagInfo.board_desc -- rather than from a table kept beside the build, so they
# cannot disagree with the image. The TagType number is named from the enum in
# proto/tagdata.proto. An image without a record (a base, a loader) records
# null for both; REQUIRE_IDENTITY, set for distributed tags, makes that an
# error instead, since a released image that cannot say what it is for is the
# failure these fields exist to prevent.

cmake_minimum_required(VERSION 3.20)

foreach(_required NAME OUTPUT SOURCE_DIR ARTIFACT_DIR)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "WriteBuildManifest.cmake: ${_required} is required")
  endif()
endforeach()

find_package(Git QUIET)

function(_git out_var)
  set(${out_var} "" PARENT_SCOPE)
  if(NOT GIT_FOUND)
    return()
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE _out
    ERROR_QUIET
    RESULT_VARIABLE _result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_result EQUAL 0)
    set(${out_var} "${_out}" PARENT_SCOPE)
  endif()
endfunction()

_git(_commit rev-parse HEAD)
_git(_short rev-parse --short HEAD)
_git(_date log -1 --format=%ad --date=iso-strict)
_git(_repo config --get remote.origin.url)
_git(_tag describe --tags --always --dirty)
_git(_porcelain status --porcelain --untracked-files=no)

set(_dirty "false")
if(_porcelain)
  set(_dirty "true")
endif()

# The ChibiOS commit, read from the submodule itself rather than from the
# superproject's recorded pointer: the recorded pointer is what should have been
# compiled, the checked-out commit is what was.
set(_chibios "")
if(GIT_FOUND AND EXISTS "${SOURCE_DIR}/ChibiOS/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}/ChibiOS"
    OUTPUT_VARIABLE _chibios
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${SOURCE_DIR}/ChibiOS"
    OUTPUT_VARIABLE _chibios_porcelain
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
set(_chibios_dirty "false")
if(_chibios_porcelain)
  set(_chibios_dirty "true")
endif()

# The branch .gitmodules says the submodule tracks, and what the commit
# describes as. A bare SHA says nothing to a reader three years from now about
# whether it was the release branch or a development one.
set(_chibios_branch "")
set(_chibios_describe "")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" config -f .gitmodules --get submodule.ChibiOS.branch
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE _chibios_branch
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(EXISTS "${SOURCE_DIR}/ChibiOS/.git")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always HEAD
      WORKING_DIRECTORY "${SOURCE_DIR}/ChibiOS"
      OUTPUT_VARIABLE _chibios_describe
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()

# Hash every artifact that exists. A missing one is reported as absent rather
# than omitted, so a truncated build is visible in the manifest.
set(_artifact_entries "")
foreach(_ext elf bin hex)
  set(_path "${ARTIFACT_DIR}/${NAME}.${_ext}")
  if(EXISTS "${_path}")
    file(SHA256 "${_path}" _hash)
    file(SIZE "${_path}" _size)
    list(APPEND _artifact_entries
         "    \"${NAME}.${_ext}\": { \"sha256\": \"${_hash}\", \"bytes\": ${_size} }")
  else()
    list(APPEND _artifact_entries "    \"${NAME}.${_ext}\": null")
  endif()
endforeach()
list(JOIN _artifact_entries ",\n" _artifacts_json)

#[[
  @brief  Read an unsigned little-endian integer out of a hex dump.

  @param  hex     Hex string from file(READ ... HEX), two digits per byte.
  @param  offset  Byte offset of the integer within @p hex.
  @param  bytes   Width of the integer in bytes (2 or 4).
  @param  out_var Receives the value as a decimal number.
#]]
function(_le_uint hex offset bytes out_var)
  set(_value "")
  math(EXPR _last "${bytes} - 1")
  foreach(_i RANGE ${_last})
    math(EXPR _pos "(${offset} + ${_i}) * 2")
    string(SUBSTRING "${hex}" ${_pos} 2 _byte)
    set(_value "${_byte}${_value}")
  endforeach()
  math(EXPR _value "0x${_value}")
  set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

#[[
  @brief  Decode a NUL-terminated string out of a hex dump.

  @param  hex     Hex string from file(READ ... HEX).
  @param  offset  Byte offset of the first character.
  @param  bytes   Bytes available; decoding stops at the first NUL or here.
  @param  out_var Receives the string, escaped for a JSON string literal.
#]]
function(_hex_cstring hex offset bytes out_var)
  set(_text "")
  math(EXPR _last "${offset} + ${bytes} - 1")
  foreach(_i RANGE ${offset} ${_last})
    math(EXPR _pos "${_i} * 2")
    string(SUBSTRING "${hex}" ${_pos} 2 _byte)
    if(_byte STREQUAL "00")
      break()
    endif()
    math(EXPR _code "0x${_byte}")
    string(ASCII ${_code} _char)
    string(APPEND _text "${_char}")
  endforeach()
  string(REPLACE "\\" "\\\\" _text "${_text}")
  string(REPLACE "\"" "\\\"" _text "${_text}")
  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

# The record sits right after the interrupt vectors: 0x1A0 bytes into flash on
# STM32L432, 0x240 on STM32U375. Its header is magic "TGID", a u16 format
# version and a u16 total size; entries {u16 id; u16 length; value} follow,
# ending with id 0xFFFF. 0x0104 is BOARD_NAME; 0x0201 starts with the TagType.
set(_tag_type_json "null")
set(_board_json "null")
set(_image_bin "${ARTIFACT_DIR}/${NAME}.bin")
if(EXISTS "${_image_bin}")
  file(SIZE "${_image_bin}" _image_size)
  foreach(_record_offset 416 576)
    math(EXPR _header_end "${_record_offset} + 8")
    if(_header_end GREATER _image_size)
      continue()
    endif()
    file(READ "${_image_bin}" _header HEX OFFSET ${_record_offset} LIMIT 8)
    if(NOT _header MATCHES "^54474944")
      continue()
    endif()
    _le_uint("${_header}" 6 2 _record_size)
    file(READ "${_image_bin}" _record HEX OFFSET ${_record_offset}
         LIMIT ${_record_size})
    set(_tag_type_number "")
    set(_entry 8)
    while(_entry LESS _record_size)
      _le_uint("${_record}" ${_entry} 2 _id)
      math(EXPR _length_at "${_entry} + 2")
      _le_uint("${_record}" ${_length_at} 2 _length)
      math(EXPR _value_at "${_entry} + 4")
      if(_id EQUAL 65535)
        break()
      elseif(_id EQUAL 260 AND _length GREATER 0)
        _hex_cstring("${_record}" ${_value_at} ${_length} _board)
        set(_board_json "\"${_board}\"")
      elseif(_id EQUAL 513 AND _length GREATER_EQUAL 4)
        _le_uint("${_record}" ${_value_at} 4 _tag_type_number)
      endif()
      math(EXPR _entry "${_value_at} + ${_length}")
    endwhile()
    if(NOT _tag_type_number STREQUAL "")
      file(READ "${SOURCE_DIR}/proto/tagdata.proto" _proto)
      if(_proto MATCHES "enum TagType {([^}]*)}")
        string(REGEX MATCH "([A-Z0-9_]+) *= *${_tag_type_number} *;"
               _tag_type_match "${CMAKE_MATCH_1}")
        if(_tag_type_match)
          set(_tag_type_json "\"${CMAKE_MATCH_1}\"")
        endif()
      endif()
      if(_tag_type_json STREQUAL "null")
        message(WARNING "${NAME}: TagType ${_tag_type_number} is not in "
                        "proto/tagdata.proto")
      endif()
    endif()
    break()
  endforeach()
endif()
if(REQUIRE_IDENTITY)
  if(_tag_type_json STREQUAL "null" OR _tag_type_json STREQUAL "\"TAG_UNSPECIFIED\""
     OR _board_json STREQUAL "null" OR _board_json STREQUAL "\"unknown\"")
    message(FATAL_ERROR
      "${NAME}: the identity record in ${_image_bin} gives no usable tag_type "
      "(${_tag_type_json}) or board (${_board_json}). A distributed image must "
      "say which tag it is for; see embedded/tags/common/core/inc/tag_identity.h.")
  endif()
endif()

string(TIMESTAMP _built_at "%Y-%m-%dT%H:%M:%SZ" UTC)

file(WRITE "${OUTPUT}"
"{
  \"target\": \"${NAME}\",
  \"tag_type\": ${_tag_type_json},
  \"board\": ${_board_json},
  \"built_at\": \"${_built_at}\",
  \"source\": {
    \"repo\": \"${_repo}\",
    \"commit\": \"${_commit}\",
    \"commit_short\": \"${_short}\",
    \"commit_date\": \"${_date}\",
    \"describe\": \"${_tag}\",
    \"dirty\": ${_dirty}
  },
  \"submodules\": {
    \"ChibiOS\": {
      \"commit\": \"${_chibios}\",
      \"describe\": \"${_chibios_describe}\",
      \"tracks_branch\": \"${_chibios_branch}\",
      \"dirty\": ${_chibios_dirty}
    }
  },
  \"tools\": {
    \"arm_gcc\": { \"path\": \"${ARM_TOOLCHAIN}\", \"version\": \"${ARM_TOOLCHAIN_VERSION}\" },
    \"nanopb_runtime\": \"${NANOPB_RUNTIME_VERSION}\",
    \"nanopb_generator\": \"${NANOPB_GENERATOR_VERSION}\"
  },
  \"build\": {
    \"reproducible_mode\": ${REPRODUCIBLE_BUILD},
    \"regenerate_sources\": \"${REGENERATE_SOURCES}\",
    \"generated_sources_committed\": ${SOURCES_COMMITTED}
  },
  \"artifacts\": {
${_artifacts_json}
  }
}
")
