find_package(Git)
message("build version.h")

execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_HASH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_SHA
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# the date of the commit
#
# iso-strict, not local: --date=local renders in the builder's timezone, so the
# same commit produces a different string on two machines. The author date and
# its offset are stored in the commit, so this is a property of the commit
# rather than of the build -- which is what lets the image carry it.

execute_process(COMMAND
    "${GIT_EXECUTABLE}" log -1 --format=%ad --date=iso-strict
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_DATE
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# the subject of the commit

execute_process(COMMAND
    "${GIT_EXECUTABLE}" log -1 --format=%s
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_SUBJECT
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# get the repo url

execute_process(COMMAND
    "${GIT_EXECUTABLE}" config --get remote.origin.url
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_REPO
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# whether the tree had uncommitted changes when this image was built
#
# A commit does not identify an image: uncommitted changes are invisible to
# rev-parse, so VERSION_HASH and SHAStr can name a commit that never produced
# these bytes. Carrying the flag lets a returned tag say so rather than have it
# silently assumed away.

execute_process(COMMAND
    "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_PORCELAIN
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

if(GIT_PORCELAIN)
  set(GIT_DIRTY 1)
  set(GIT_DIRTY_STR "dirty")
else()
  set(GIT_DIRTY 0)
  set(GIT_DIRTY_STR "clean")
endif()

# the ChibiOS commit actually compiled, which is not necessarily the one the
# superproject records

execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../ChibiOS"
    OUTPUT_VARIABLE CHIBIOS_SHA
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# generate version.h 

configure_file("${CMAKE_CURRENT_LIST_DIR}/version.h.in" "version.h" @ONLY)
