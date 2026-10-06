#[[
  @file   MacosCodesignBundle.cmake
  @brief  Signs one deployed macOS app bundle, inside out.

  @details The single implementation of how this project signs a bundle. Two
           callers share it so that a locally built package and a CI-built one
           re-signed on a developer's Mac carry identical signatures:

           - install_macos_codesign() in DeployQt.cmake includes this file
             from its install(CODE) and calls macos_codesign_bundle() on each
             installed bundle;
           - host/tools/sign-ci-macos.sh runs it in script mode (-P) on each
             bundle copied out of a CI-built DMG.

           Script mode takes the same arguments as -D variables:

             cmake -DBUNDLE=<path.app> -DIDENTITY=<identity>
                   [-DENTITLEMENTS=<plist>] [-DCODESIGN=<codesign>]
                   -P cmake/MacosCodesignBundle.cmake

  @see    docs/decisions/0024-release-macos-package-built-on-ci-signed-locally.md
#]]

#[[
  @brief  Re-signs every Mach-O in a bundle, then seals the bundle.

  @details Removes any existing signature first: a bundle out of
           macdeployqt or a CI build carries ad-hoc signatures that must not
           survive under the new seal. Signs nested dylibs, plugins and QML
           plugins, then frameworks, then the executables in Contents/MacOS,
           and last the bundle itself, which seals the resources. Symlinks are
           skipped; their targets are signed.

           A real identity gets the hardened runtime on every item and a
           secure timestamp on the bundle; nested items are signed with
           --timestamp=none. An ad-hoc identity ("-") carries no team
           identifier, so it can take neither: it gets --timestamp=none and no
           hardened runtime. Ad-hoc signing exists only to make a bundle
           launchable on Apple Silicon, which refuses any binary whose
           signature does not validate; notarization is what needs the
           hardened runtime, and an ad-hoc build is not notarizable anyway.

           Finishes with codesign --verify --deep --strict.

  @param  BUNDLE        Absolute path of the .app bundle to sign.
  @param  IDENTITY      codesign identity, or "-" for ad-hoc.
  @param  ENTITLEMENTS  Optional entitlements plist applied to the bundle.
  @param  CODESIGN      Optional codesign executable; taken from PATH if empty.

  Fails with FATAL_ERROR if the bundle is missing, any item fails to sign
  (including when codesign cannot be run), or the result does not verify.

  Example:
    macos_codesign_bundle(BUNDLE /tmp/x/tag-info.app IDENTITY "-")
#]]
function(macos_codesign_bundle)
    cmake_parse_arguments(_mcs "" "BUNDLE;IDENTITY;ENTITLEMENTS;CODESIGN" "" ${ARGN})

    if(NOT _mcs_BUNDLE OR NOT _mcs_IDENTITY)
        message(FATAL_ERROR "macos_codesign_bundle: BUNDLE and IDENTITY are required")
    endif()
    if(NOT EXISTS "${_mcs_BUNDLE}")
        message(FATAL_ERROR "Cannot sign missing bundle: ${_mcs_BUNDLE}")
    endif()
    # find_program finds nothing in script mode, so with no CODESIGN given,
    # rely on execute_process searching PATH.
    set(_codesign "${_mcs_CODESIGN}")
    if(NOT _codesign)
        set(_codesign codesign)
    endif()

    set(_item_hardened --options runtime)
    set(_bundle_hardened --options runtime)
    set(_bundle_timestamp --timestamp)
    if(_mcs_IDENTITY STREQUAL "-")
        set(_item_hardened "")
        set(_bundle_hardened "")
        set(_bundle_timestamp --timestamp=none)
    endif()
    set(_entitlements "")
    if(_mcs_ENTITLEMENTS)
        set(_entitlements --entitlements "${_mcs_ENTITLEMENTS}")
    endif()

    get_filename_component(_bundle_name "${_mcs_BUNDLE}" NAME)
    message(STATUS "Signing ${_bundle_name}")

    cmake_policy(PUSH)
    cmake_policy(SET CMP0009 NEW)
    file(GLOB _frameworks "${_mcs_BUNDLE}/Contents/Frameworks/*.framework")
    file(GLOB_RECURSE _macho_files
        "${_mcs_BUNDLE}/Contents/Frameworks/*.dylib"
        "${_mcs_BUNDLE}/Contents/PlugIns/*.dylib"
        "${_mcs_BUNDLE}/Contents/PlugIns/*.so"
        "${_mcs_BUNDLE}/Contents/Resources/qml/*.dylib"
        "${_mcs_BUNDLE}/Contents/Resources/qml/*.so")
    file(GLOB _executables "${_mcs_BUNDLE}/Contents/MacOS/*")
    cmake_policy(POP)

    set(_items ${_macho_files} ${_frameworks} ${_executables})
    if(_items)
        list(REMOVE_DUPLICATES _items)
    endif()

    foreach(_item IN LISTS _items)
        if(EXISTS "${_item}" AND NOT IS_SYMLINK "${_item}")
            execute_process(
                COMMAND "${_codesign}" --remove-signature "${_item}"
                ERROR_QUIET)
        endif()
    endforeach()
    execute_process(
        COMMAND "${_codesign}" --remove-signature "${_mcs_BUNDLE}"
        ERROR_QUIET)

    foreach(_item IN LISTS _items)
        if(EXISTS "${_item}" AND NOT IS_SYMLINK "${_item}")
            execute_process(
                COMMAND "${_codesign}" --force --timestamp=none ${_item_hardened}
                        --sign "${_mcs_IDENTITY}" "${_item}"
                RESULT_VARIABLE _result)
            if(NOT _result EQUAL 0)
                message(FATAL_ERROR "codesign failed for ${_item}")
            endif()
        endif()
    endforeach()

    execute_process(
        COMMAND "${_codesign}" --force ${_bundle_timestamp} ${_bundle_hardened}
                ${_entitlements} --sign "${_mcs_IDENTITY}" "${_mcs_BUNDLE}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "codesign failed for ${_bundle_name}")
    endif()

    execute_process(
        COMMAND "${_codesign}" --verify --deep --strict "${_mcs_BUNDLE}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        execute_process(
            COMMAND "${_codesign}" --verify --deep --strict --verbose=4 "${_mcs_BUNDLE}")
        message(FATAL_ERROR "codesign verification failed for ${_bundle_name}")
    endif()
endfunction()

# Script mode: `cmake -DBUNDLE=... -DIDENTITY=... -P MacosCodesignBundle.cmake`.
# When included from an install script, CMAKE_SCRIPT_MODE_FILE names that
# script instead, and only the function is defined.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    macos_codesign_bundle(
        BUNDLE "${BUNDLE}"
        IDENTITY "${IDENTITY}"
        ENTITLEMENTS "${ENTITLEMENTS}"
        CODESIGN "${CODESIGN}")
endif()
