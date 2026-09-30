# Configure-time checks for conditions that make a firmware build
# unreproducible.
#
# Each condition is detected here, reported through reproducibility_problem() so
# that -DREPRODUCIBLE_BUILD=ON turns it into an error, and left in a variable so
# a later build manifest can record it.  Detection that only reaches a log line
# nobody reads is worth little, which is why these are warnings by default and
# errors in the mode CI and release qualification use.
#
# Everything here is best-effort: a build from an exported tarball has no git
# metadata, and that is a gap in the evidence rather than proof of a problem, so
# it is reported once and the git checks are skipped.

# Results are recorded as global properties rather than returned: PARENT_SCOPE
# reaches only one level and these helpers nest, and the build manifest that
# will read them is written elsewhere again.
#
#   ULTRALIGHT_TREE_DIRTY            yes | no | unknown
#   ULTRALIGHT_CHIBIOS_SHA           submodule commit, when it can be read
#   ULTRALIGHT_ARM_TOOLCHAIN_VERSION compiler version, when it can be read

function(check_reproducibility_pitfalls)
  set_property(GLOBAL PROPERTY ULTRALIGHT_TREE_DIRTY "unknown")
  set_property(GLOBAL PROPERTY ULTRALIGHT_CHIBIOS_SHA "")
  set_property(GLOBAL PROPERTY ULTRALIGHT_ARM_TOOLCHAIN_VERSION "")
  set_property(GLOBAL PROPERTY ULTRALIGHT_FMPP_VERSION "")

  _check_arm_toolchain()
  _check_fmpp()
  _check_git_provenance()
endfunction()

# -- ARM toolchain ------------------------------------------------------------
#
# A different compiler is a different image, so the version is recorded whether
# or not it is checked.  ARM_TOOLCHAIN_VERSION is empty by default: recording
# the version is unambiguously right, while refusing to build on a different one
# is a policy the group has to choose, so the comparison is opt-in.

function(_check_arm_toolchain)
  if(NOT GCCARM)
    return()
  endif()

  execute_process(
    COMMAND "${GCCARM}" -dumpversion
    OUTPUT_VARIABLE _version
    ERROR_VARIABLE _version_err
    RESULT_VARIABLE _version_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _version_result EQUAL 0 OR "${_version}" STREQUAL "")
    message(WARNING
      "Could not determine the version of ${GCCARM}; the compiler that built "
      "these images will not be recorded.")
    return()
  endif()

  set_property(GLOBAL PROPERTY ULTRALIGHT_ARM_TOOLCHAIN_VERSION "${_version}")

  if(ARM_TOOLCHAIN_VERSION AND NOT "${_version}" STREQUAL "${ARM_TOOLCHAIN_VERSION}")
    reproducibility_problem(
      "The ARM toolchain is version ${_version} but ARM_TOOLCHAIN_VERSION pins "
      "${ARM_TOOLCHAIN_VERSION}. A different compiler produces a different "
      "image from the same sources. Install the pinned toolchain, or change the "
      "pin deliberately.")
  else()
    message(STATUS "ARM toolchain: ${GCCARM} (version ${_version})")
  endif()
endfunction()

# -- fmpp ---------------------------------------------------------------------
#
# fmpp renders the ChibiOS board templates.  Only a board that regenerates needs
# it, so it is looked up leniently and this check is skipped when nothing can
# regenerate.
#
# The version is compared here rather than recorded in the board manifests.  A
# manifest has to compare equal across machines, and a version string reported
# on one and not another would make every board look stale for a reason
# unrelated to the tree.  Comparing it at configure time has no such cost: it
# constrains the machine doing the rendering without putting anything
# machine-specific into a committed file.

function(_check_fmpp)
  if(NOT FMPP OR REGENERATE_SOURCES STREQUAL "OFF")
    return()
  endif()

  execute_process(
    COMMAND "${FMPP}" --version
    OUTPUT_VARIABLE _fmpp_output
    ERROR_VARIABLE _fmpp_output_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _fmpp_ran)
  if(NOT _fmpp_ran EQUAL 0)
    message(WARNING
      "Could not run ${FMPP} --version, so the renderer is unverified.")
    return()
  endif()

  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?"
         _fmpp_version "${_fmpp_output}${_fmpp_output_err}")
  if(NOT _fmpp_version)
    message(WARNING
      "Could not read a version from `${FMPP} --version`, which printed: "
      "${_fmpp_output}${_fmpp_output_err}")
    return()
  endif()

  set_property(GLOBAL PROPERTY ULTRALIGHT_FMPP_VERSION "${_fmpp_version}")

  if(FMPP_VERSION AND NOT "${_fmpp_version}" STREQUAL "${FMPP_VERSION}")
    reproducibility_problem(
      "fmpp is version ${_fmpp_version} but FMPP_VERSION pins ${FMPP_VERSION}. "
      "A different renderer could produce different board files from the same "
      "templates, and those files are committed. Install the pinned version, "
      "or change the pin deliberately and read the regenerated diff.")
  else()
    message(STATUS "fmpp: ${FMPP} (version ${_fmpp_version})")
  endif()
endfunction()

# -- git provenance -----------------------------------------------------------

function(_check_git_provenance)
  find_package(Git QUIET)
  if(NOT GIT_FOUND)
    message(STATUS
      "git was not found; the working tree and submodule state cannot be "
      "checked and will not be recorded.")
    return()
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _in_work_tree
    ERROR_QUIET
    RESULT_VARIABLE _work_tree_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _work_tree_result EQUAL 0 OR NOT _in_work_tree STREQUAL "true")
    message(STATUS
      "Not a git working tree; the working tree and submodule state cannot be "
      "checked and will not be recorded.")
    return()
  endif()

  # Modified tracked files, not untracked ones: an untracked file is not
  # compiled into anything unless it is also listed somewhere, and treating
  # scratch files as a reproducibility failure would make the warning routine
  # and so ignorable.

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _dirty
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(_dirty)
    set_property(GLOBAL PROPERTY ULTRALIGHT_TREE_DIRTY "yes")
    _summarize_paths("${_dirty}" _dirty_summary)
    reproducibility_problem(
      "The working tree has uncommitted changes, so the commit does not "
      "describe the sources these images were built from: ${_dirty_summary}")
  else()
    set_property(GLOBAL PROPERTY ULTRALIGHT_TREE_DIRTY "no")
  endif()

  # The vendored nanopb runtime is compiled into every image, so an edit here is
  # a local patch shipped on tags. It is inside the dirty check above, but
  # calling it out by name means the message says what actually happened.

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            -- "embedded/thirdparty"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _vendored
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(_vendored)
    _summarize_paths("${_vendored}" _vendored_summary)
    reproducibility_problem(
      "The vendored nanopb runtime has local modifications, which would be "
      "compiled into every image built here: ${_vendored_summary}")
  endif()

  _check_submodules()

  # Building at an untagged commit is normal during development and says nothing
  # by itself, so it is reported only in the strict mode, where the build is
  # meant to be a release candidate.

  if(REPRODUCIBLE_BUILD)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --exact-match --tags HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _exact_tag
      ERROR_QUIET
      RESULT_VARIABLE _exact_tag_result
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _exact_tag_result EQUAL 0)
      message(WARNING
        "REPRODUCIBLE_BUILD=ON but HEAD is not at a tag, so these images are "
        "not a release candidate and nothing names the version they carry.")
    else()
      message(STATUS "Building at tag ${_exact_tag}")
    endif()
  endif()
endfunction()

# -- submodules ---------------------------------------------------------------
#
# `git submodule status` marks each submodule: '-' not initialized, '+' checked
# out at a commit other than the one recorded, 'U' unmerged. Any of those means
# the library being compiled is not the one the superproject commit names.

function(_check_submodules)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" submodule status
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _status
    ERROR_QUIET
    RESULT_VARIABLE _status_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _status_result EQUAL 0 OR "${_status}" STREQUAL "")
    return()
  endif()

  string(REGEX REPLACE "\r?\n" ";" _lines "${_status}")
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^([-+U ])([0-9a-f]+) +([^ ]+)")
      set(_mark "${CMAKE_MATCH_1}")
      set(_sha "${CMAKE_MATCH_2}")
      set(_path "${CMAKE_MATCH_3}")

      if(_path STREQUAL "ChibiOS")
        set_property(GLOBAL PROPERTY ULTRALIGHT_CHIBIOS_SHA "${_sha}")
      endif()

      if(_mark STREQUAL "-")
        reproducibility_problem(
          "Submodule ${_path} is not initialized. Run "
          "`git submodule update --init --recursive ${_path}`.")
      elseif(_mark STREQUAL "+")
        reproducibility_problem(
          "Submodule ${_path} is checked out at ${_sha}, which is not the "
          "commit this tree records. The library being compiled is not the one "
          "this commit names. Run `git submodule update ${_path}`, or commit "
          "the new pointer deliberately.")
      elseif(_mark STREQUAL "U")
        reproducibility_problem(
          "Submodule ${_path} has unmerged conflicts.")
      endif()

      _check_submodule_branch("${_path}")

      # A submodule can sit at the recorded commit and still have edits in it,
      # which the superproject's own status does not show as file changes.
      if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${_path}/.git"
         OR EXISTS "${CMAKE_SOURCE_DIR}/${_path}/.git")
        execute_process(
          COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
          WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${_path}"
          OUTPUT_VARIABLE _sub_dirty
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_sub_dirty)
          _summarize_paths("${_sub_dirty}" _sub_summary)
          reproducibility_problem(
            "Submodule ${_path} has uncommitted changes, invisible to the "
            "superproject: ${_sub_summary}")
        endif()
      endif()
    endif()
  endforeach()
endfunction()

# The recorded commit can be the one checked out and still be the wrong commit:
# nothing above notices a pointer moved from stable_21.11.x to master, because
# both are "the commit this tree records". .gitmodules already names the branch
# each submodule is meant to track, so that is the thing to check against.

function(_check_submodule_branch path)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" config -f .gitmodules
            --get "submodule.${path}.branch"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _branch
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if("${_branch}" STREQUAL "")
    return()
  endif()

  # Prefer the remote-tracking ref: a local branch of the same name can have
  # been moved, while origin/<branch> is what upstream actually publishes.
  set(_ref "")
  foreach(_candidate "origin/${_branch}" "${_branch}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --verify --quiet "${_candidate}^{commit}"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${path}"
      OUTPUT_QUIET ERROR_QUIET
      RESULT_VARIABLE _candidate_result)
    if(_candidate_result EQUAL 0)
      set(_ref "${_candidate}")
      break()
    endif()
  endforeach()

  if("${_ref}" STREQUAL "")
    # A shallow or single-branch clone has no such ref. That is a gap in the
    # evidence, not a mismatch, so say so rather than fail.
    message(STATUS
      "Submodule ${path} should track ${_branch}, but no ref for it is present "
      "in this clone, so that cannot be confirmed here.")
    return()
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" merge-base --is-ancestor HEAD "${_ref}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${path}"
    RESULT_VARIABLE _contained
    OUTPUT_QUIET ERROR_QUIET)

  if(NOT _contained EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${path}"
      OUTPUT_VARIABLE _describe
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    reproducibility_problem(
      "Submodule ${path} is at ${_describe}, which is not on ${_branch} -- the "
      "branch .gitmodules says it tracks. The commit is the one this tree "
      "records, so nothing else flags it, but the library is not the one the "
      "project means to build against.")
  else()
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${path}"
      OUTPUT_VARIABLE _describe
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Submodule ${path}: ${_describe} on ${_branch}")
  endif()
endfunction()

# Turn porcelain output into a short phrase. A message listing two hundred paths
# is as unread as no message at all.

function(_summarize_paths porcelain out_var)
  string(REGEX REPLACE "\r?\n" ";" _lines "${porcelain}")
  list(FILTER _lines EXCLUDE REGEX "^[ \t]*$")
  list(LENGTH _lines _count)
  set(_shown "")
  foreach(_line IN LISTS _lines)
    # Porcelain lines are two status characters and a space, then the path.
    # REGEX REPLACE is wrong here: CMake re-anchors '^' after each match, so a
    # leading-N-characters pattern strips repeatedly and leaves only the tail.
    string(REGEX MATCH "^...(.*)$" _ "${_line}")
    set(_path "${CMAKE_MATCH_1}")
    list(APPEND _shown "${_path}")
    list(LENGTH _shown _shown_count)
    if(_shown_count GREATER_EQUAL 3)
      break()
    endif()
  endforeach()
  list(JOIN _shown ", " _text)
  if(_count GREATER 3)
    math(EXPR _more "${_count} - 3")
    set(_text "${_text}, and ${_more} more")
  endif()
  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()
