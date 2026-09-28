# Write a build manifest beside a firmware image.
#
# Run as a script after the image is linked:
#
#   cmake -DNAME=... -DOUTPUT=... -DSOURCE_DIR=... -DARTIFACT_DIR=... \
#         -DNANOPB_RUNTIME_VERSION=... -DNANOPB_GENERATOR_VERSION=... \
#         -DARM_TOOLCHAIN=... -DARM_TOOLCHAIN_VERSION=... \
#         -DREPRODUCIBLE_BUILD=... -DREGENERATE_SOURCES=... \
#         -DSOURCES_COMMITTED=... -P WriteBuildManifest.cmake
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

string(TIMESTAMP _built_at "%Y-%m-%dT%H:%M:%SZ" UTC)

file(WRITE "${OUTPUT}"
"{
  \"target\": \"${NAME}\",
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
