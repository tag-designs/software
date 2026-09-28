find_package(Git)
message("build version.h")

# --short=8, not --short: git picks the shortest unambiguous abbreviation, whose
# length depends on how many objects the repository holds. A CI clone and a
# developer clone of the same commit produced 8 and 7 characters, which put
# different bytes in the image for the same source.

execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_HASH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_SHA
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# the date of the commit, in a format this file chooses rather than one git
# renders
#
# --date=local was wrong because it renders in the builder's timezone.
# --date=iso-strict was wrong more subtly: it is timezone-independent, but git
# itself renders UTC as `Z` in some versions and `+00:00` in others, so a CI
# runner and a developer machine produced different strings for the same commit.
# Supplying an explicit strftime format with TZ=UTC leaves nothing for a git
# version to decide.

execute_process(COMMAND
    "${CMAKE_COMMAND}" -E env TZ=UTC
    "${GIT_EXECUTABLE}" log -1 --format=%ad
    "--date=format-local:%Y-%m-%dT%H:%M:%SZ"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_DATE
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# the subject of the commit

execute_process(COMMAND
    "${GIT_EXECUTABLE}" log -1 --format=%s
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_SUBJECT
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# get the repo url, normalized
#
# The raw remote records how this clone was made, not which repository it is:
# CI clones over HTTPS and a developer over SSH, so the same commit carried
# `https://github.com/owner/repo` in one image and `git@github.com:owner/repo.git`
# in the other. Both name the same repository, so both are reduced to
# `host/owner/repo`.

execute_process(COMMAND
    "${GIT_EXECUTABLE}" config --get remote.origin.url
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE GIT_REPO
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

string(REGEX REPLACE "^[a-zA-Z][a-zA-Z0-9+.-]*://" "" GIT_REPO "${GIT_REPO}")
string(REGEX REPLACE "^[^@/]+@" "" GIT_REPO "${GIT_REPO}")
string(REGEX REPLACE "^([^/:]+):" "\\1/" GIT_REPO "${GIT_REPO}")
string(REGEX REPLACE "\\.git$" "" GIT_REPO "${GIT_REPO}")

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
