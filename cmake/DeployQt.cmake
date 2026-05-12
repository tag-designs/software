# The MIT License (MIT)
#
# Copyright (c) 2018 Nathan Osman
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

find_package(Qt6Core REQUIRED)

# Retrieve the absolute path to qmake and then use that path to find
# the windeployqt and macdeployqt binaries

get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
get_filename_component(_qt_root_dir "${_qt_bin_dir}" DIRECTORY)
set(_qt_plugins_dir "${_qt_root_dir}/plugins")

if (NOT WINDEPLOYQT_EXECUTABLE)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
endif()
if(WIN32 AND NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "windeployqt not found")
endif()

find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")
if(APPLE AND NOT MACDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "macdeployqt not found in ${_qt_bin_dir}")
endif()

if(APPLE AND NOT CODESIGN_EXECUTABLE)
find_program(CODESIGN_EXECUTABLE codesign)
endif()

if(APPLE AND NOT INSTALL_NAME_TOOL_EXECUTABLE)
find_program(INSTALL_NAME_TOOL_EXECUTABLE install_name_tool)
endif()

# Add commands that copy the required Qt files to the same directory as the
# target after being built as well as including them in final installation
function(windeployqt target)

    # Run windeployqt immediately after build
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E
            env PATH="${_qt_bin_dir}" "${WINDEPLOYQT_EXECUTABLE}"
                --verbose 0
                --no-compiler-runtime
                --no-opengl-sw
                \"$<TARGET_FILE:${target}>\"
        COMMENT "Deploying Qt..."
    )

    # windeployqt doesn't work correctly with the system runtime libraries,
    # so we fall back to one of CMake's own modules for copying them over

    # Doing this with MSVC 2015 requires CMake 3.6+
    if((MSVC_VERSION VERSION_EQUAL 1900 OR MSVC_VERSION VERSION_GREATER 1900)
            AND CMAKE_VERSION VERSION_LESS "3.6")
        message(WARNING "Deploying with MSVC 2015+ requires CMake 3.6+")
    endif()

    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
    foreach(lib ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
        get_filename_component(filename "${lib}" NAME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E
                copy_if_different "${lib}" \"$<TARGET_FILE_DIR:${target}>\"
            COMMENT "Copying ${filename}...\n"
        )
    endforeach()
endfunction()

# Add commands that copy the required Qt files to the application bundle
# represented by the target.
function(macdeployqt target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${MACDEPLOYQT_EXECUTABLE}"
            \"$<TARGET_FILE_DIR:${target}>/../..\"
            -always-overwrite
        COMMENT "Deploying Qt..."
    )
endfunction()

function(install_windeployqt target)
    if(NOT HOST_WINDOWS_APPDIR)
        set(HOST_WINDOWS_APPDIR "${CMAKE_INSTALL_BINDIR}")
    endif()
    if(NOT HOST_WINDOWS_LIBDIR)
        set(HOST_WINDOWS_LIBDIR "${CMAKE_INSTALL_BINDIR}")
    endif()
    if(NOT HOST_WINDOWS_PLUGINDIR)
        set(HOST_WINDOWS_PLUGINDIR "${CMAKE_INSTALL_BINDIR}/plugins")
    endif()
    if(NOT HOST_WINDOWS_QMLDIR)
        set(HOST_WINDOWS_QMLDIR "${CMAKE_INSTALL_BINDIR}/qml")
    endif()
    get_target_property(_target_output_name ${target} OUTPUT_NAME)
    if(NOT _target_output_name)
        set(_target_output_name ${target})
    endif()
    set(_windeployqt_qml_options "")
    foreach(_qml_dir IN LISTS ARGN)
        string(APPEND _windeployqt_qml_options "                    --qmldir \"${_qml_dir}\"\n")
    endforeach()

    install(CODE "
        message(STATUS \"Deploying Qt runtime for ${_target_output_name}\")
        execute_process(
            COMMAND \"${CMAKE_COMMAND}\" -E env \"PATH=${_qt_bin_dir}\" \"${WINDEPLOYQT_EXECUTABLE}\"
                    --verbose 0
                    --no-compiler-runtime
                    --no-opengl-sw
                    --no-translations
                    --dir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
                    --libdir \"\${CMAKE_INSTALL_PREFIX}/${HOST_WINDOWS_LIBDIR}\"
                    --plugindir \"\${CMAKE_INSTALL_PREFIX}/${HOST_WINDOWS_PLUGINDIR}\"
                    --qml-deploy-dir \"\${CMAKE_INSTALL_PREFIX}/${HOST_WINDOWS_QMLDIR}\"
${_windeployqt_qml_options}
                    \"\${CMAKE_INSTALL_PREFIX}/${HOST_WINDOWS_APPDIR}/${_target_output_name}${CMAKE_EXECUTABLE_SUFFIX}\"
            RESULT_VARIABLE _windeployqt_result)
        if(NOT _windeployqt_result EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed for ${_target_output_name}\")
        endif()
        file(WRITE \"\${CMAKE_INSTALL_PREFIX}/${HOST_WINDOWS_APPDIR}/qt.conf\"
\"[Paths]
Plugins=../plugins
Qml2Imports=../qml
\")
    ")

endfunction()

function(install_macdeployqt target)
    get_target_property(_target_output_name ${target} OUTPUT_NAME)
    if(NOT _target_output_name)
        set(_target_output_name ${target})
    endif()
    if(NOT HOST_BUNDLE_INSTALL_DIR)
        set(HOST_BUNDLE_INSTALL_DIR ".")
    endif()
    set(_macdeployqt_options -always-overwrite)
    if(MACOS_DEPLOY_QT_PLUGINS_MANUALLY)
        list(APPEND _macdeployqt_options -no-plugins)
    endif()
    if(MACOS_USE_MACDEPLOYQT_DEPLOYMENT AND MACOS_SIGN_APPS AND MACOS_CODE_SIGN_IDENTITY)
        list(APPEND _macdeployqt_options "-codesign=${MACOS_CODE_SIGN_IDENTITY}")
    endif()
    if(MACOS_DEPLOY_APPSTORE_COMPLIANT)
        list(APPEND _macdeployqt_options -appstore-compliant)
    endif()
    if(MACOS_MACDEPLOYQT_EXTRA_OPTIONS)
        list(APPEND _macdeployqt_options ${MACOS_MACDEPLOYQT_EXTRA_OPTIONS})
    endif()
    foreach(_qml_dir IN LISTS ARGN)
        list(APPEND _macdeployqt_options "-qmldir=${_qml_dir}")
    endforeach()

    set(_macdeployqt_option_lines "")
    foreach(option IN LISTS _macdeployqt_options)
        string(APPEND _macdeployqt_option_lines "                    \"${option}\"\n")
    endforeach()

    install(CODE "
        message(STATUS \"Deploying Qt runtime for ${_target_output_name}.app\")
        execute_process(
            COMMAND \"${MACDEPLOYQT_EXECUTABLE}\"
                    \"\${CMAKE_INSTALL_PREFIX}/${HOST_BUNDLE_INSTALL_DIR}/${_target_output_name}.app\"
${_macdeployqt_option_lines}
            RESULT_VARIABLE _macdeployqt_result)
        if(NOT _macdeployqt_result EQUAL 0)
            message(FATAL_ERROR \"macdeployqt failed for ${_target_output_name}.app\")
        endif()
    ")
endfunction()

function(install_macos_qt_plugins target)
    if(NOT MACOS_DEPLOY_QT_PLUGINS_MANUALLY)
        return()
    endif()

    get_target_property(_target_output_name ${target} OUTPUT_NAME)
    if(NOT _target_output_name)
        set(_target_output_name ${target})
    endif()
    if(NOT HOST_BUNDLE_INSTALL_DIR)
        set(HOST_BUNDLE_INSTALL_DIR ".")
    endif()

    install(CODE "
        set(_bundle_plugins_dir \"\${CMAKE_INSTALL_PREFIX}/${HOST_BUNDLE_INSTALL_DIR}/${_target_output_name}.app/Contents/PlugIns\")
        file(MAKE_DIRECTORY \"\${_bundle_plugins_dir}\")

        set(_plugin_files
            \"${_qt_plugins_dir}/platforms/libqcocoa.dylib\"
            \"${_qt_plugins_dir}/printsupport/libcocoaprintersupport.dylib\"
            \"${_qt_plugins_dir}/iconengines/libqsvgicon.dylib\")

        file(GLOB _imageformat_plugins \"${_qt_plugins_dir}/imageformats/*.dylib\")
        list(APPEND _plugin_files \${_imageformat_plugins})

        foreach(_plugin_file IN LISTS _plugin_files)
            if(EXISTS \"\${_plugin_file}\")
                get_filename_component(_plugin_type \"\${_plugin_file}\" DIRECTORY)
                get_filename_component(_plugin_type \"\${_plugin_type}\" NAME)
                file(MAKE_DIRECTORY \"\${_bundle_plugins_dir}/\${_plugin_type}\")
                file(COPY \"\${_plugin_file}\" DESTINATION \"\${_bundle_plugins_dir}/\${_plugin_type}\")
            endif()
        endforeach()
    ")
endfunction()

function(install_macos_remove_bundle_signatures target)
    if(NOT MACOS_SIGN_APPS)
        return()
    endif()
    if(NOT CODESIGN_EXECUTABLE)
        message(FATAL_ERROR "codesign not found")
    endif()

    get_target_property(_target_output_name ${target} OUTPUT_NAME)
    if(NOT _target_output_name)
        set(_target_output_name ${target})
    endif()
    if(NOT HOST_BUNDLE_INSTALL_DIR)
        set(HOST_BUNDLE_INSTALL_DIR ".")
    endif()

    install(CODE "
        if(POLICY CMP0009)
            cmake_policy(SET CMP0009 NEW)
        endif()
        set(_bundle_path \"\${CMAKE_INSTALL_PREFIX}/${HOST_BUNDLE_INSTALL_DIR}/${_target_output_name}.app\")
        if(EXISTS \"\${_bundle_path}\")
            message(STATUS \"Removing pre-existing signatures from ${_target_output_name}.app before dependency fixup\")
            file(GLOB_RECURSE _bundle_signable_items
                \"\${_bundle_path}/Contents/MacOS/*\"
                \"\${_bundle_path}/Contents/Frameworks/*\"
                \"\${_bundle_path}/Contents/PlugIns/*.dylib\"
                \"\${_bundle_path}/Contents/PlugIns/*/*.dylib\")
            foreach(_bundle_item IN LISTS _bundle_signable_items)
                if(NOT IS_DIRECTORY \"\${_bundle_item}\")
                    execute_process(
                        COMMAND \"${CODESIGN_EXECUTABLE}\" --remove-signature \"\${_bundle_item}\"
                        RESULT_VARIABLE _remove_signature_result
                        ERROR_QUIET)
                endif()
            endforeach()
        endif()
    ")
endfunction()

function(install_macos_fixup_bundle target)
    if(NOT MACOS_FIXUP_BUNDLE_DEPENDENCIES)
        return()
    endif()
    get_target_property(_target_libraries ${target} LINK_LIBRARIES)
    if(NOT "tag" IN_LIST _target_libraries)
        return()
    endif()
    if(NOT INSTALL_NAME_TOOL_EXECUTABLE)
        message(FATAL_ERROR "install_name_tool not found")
    endif()

    get_target_property(_target_output_name ${target} OUTPUT_NAME)
    if(NOT _target_output_name)
        set(_target_output_name ${target})
    endif()
    if(NOT HOST_BUNDLE_INSTALL_DIR)
        set(HOST_BUNDLE_INSTALL_DIR ".")
    endif()

    set(_bundle_dependency_dirs
        "${_qt_root_dir}/lib"
        "/opt/homebrew/lib"
        "/usr/local/lib")
    if(MACOS_BUNDLE_FIXUP_EXTRA_DIRS)
        list(APPEND _bundle_dependency_dirs ${MACOS_BUNDLE_FIXUP_EXTRA_DIRS})
    endif()

    string(REPLACE ";" "\n                " _bundle_dependency_dirs_code "${_bundle_dependency_dirs}")

    install(CODE "
        set(_bundle_path \"\${CMAKE_INSTALL_PREFIX}/${HOST_BUNDLE_INSTALL_DIR}/${_target_output_name}.app\")
        if(EXISTS \"\${_bundle_path}\")
            message(STATUS \"Copying runtime dependencies for ${_target_output_name}.app\")
            set(_bundle_frameworks_dir \"\${_bundle_path}/Contents/Frameworks\")
            file(MAKE_DIRECTORY \"\${_bundle_frameworks_dir}\")
            set(_bundle_dependency_dirs
                ${_bundle_dependency_dirs_code})
            file(GLOB_RECURSE _bundle_libraries
                \"\${_bundle_path}/Contents/Frameworks/*.dylib\"
                \"\${_bundle_path}/Contents/PlugIns/*.dylib\")
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES \"\${_bundle_path}/Contents/MacOS/${_target_output_name}\"
                LIBRARIES \${_bundle_libraries}
                DIRECTORIES \${_bundle_dependency_dirs}
                RESOLVED_DEPENDENCIES_VAR _resolved_runtime_dependencies
                UNRESOLVED_DEPENDENCIES_VAR _unresolved_runtime_dependencies
                POST_EXCLUDE_REGEXES
                    \"^/System/Library/.*\"
                    \"^/usr/lib/.*\")
            foreach(_runtime_dependency IN LISTS _resolved_runtime_dependencies)
                if(_runtime_dependency MATCHES \"^(/opt/homebrew|/usr/local)/.*\\\\.dylib$\")
                    file(INSTALL
                        DESTINATION \"\${_bundle_frameworks_dir}\"
                        TYPE SHARED_LIBRARY
                        FILES \"\${_runtime_dependency}\")
                endif()
            endforeach()
            foreach(_runtime_dependency IN LISTS _unresolved_runtime_dependencies)
                if(_runtime_dependency MATCHES \"^(@executable_path|@loader_path|@rpath)/.*\\\\.dylib$\")
                    get_filename_component(_runtime_dependency_name \"\${_runtime_dependency}\" NAME)
                    set(_runtime_dependency_source \"\")
                    foreach(_dependency_dir IN LISTS _bundle_dependency_dirs)
                        if(EXISTS \"\${_dependency_dir}/\${_runtime_dependency_name}\")
                            set(_runtime_dependency_source \"\${_dependency_dir}/\${_runtime_dependency_name}\")
                            break()
                        endif()
                    endforeach()
                    if(_runtime_dependency_source)
                        file(INSTALL
                            DESTINATION \"\${_bundle_frameworks_dir}\"
                            TYPE SHARED_LIBRARY
                            FILES \"\${_runtime_dependency_source}\")
                        list(REMOVE_ITEM _unresolved_runtime_dependencies \"\${_runtime_dependency}\")
                    endif()
                endif()
            endforeach()
            foreach(_runtime_dependency IN LISTS _unresolved_runtime_dependencies)
                if(_runtime_dependency MATCHES \"^@rpath/Qt.*\\\\.framework/.*\")
                    list(REMOVE_ITEM _unresolved_runtime_dependencies \"\${_runtime_dependency}\")
                endif()
            endforeach()
            file(GLOB_RECURSE _bundle_rewrite_items
                \"\${_bundle_path}/Contents/Frameworks/*.dylib\"
                \"\${_bundle_path}/Contents/PlugIns/*.dylib\"
                \"\${_bundle_path}/Contents/PlugIns/*/*.dylib\")
            foreach(_bundle_rewrite_item IN LISTS _bundle_rewrite_items)
                get_filename_component(_bundle_rewrite_name \"\${_bundle_rewrite_item}\" NAME)
                if(_bundle_rewrite_item MATCHES \"\\\\.dylib$\")
                    execute_process(
                        COMMAND \"${INSTALL_NAME_TOOL_EXECUTABLE}\"
                                -id \"@executable_path/../Frameworks/\${_bundle_rewrite_name}\"
                                \"\${_bundle_rewrite_item}\"
                        RESULT_VARIABLE _install_name_id_result
                        ERROR_QUIET)
                endif()

                execute_process(
                    COMMAND otool -L \"\${_bundle_rewrite_item}\"
                    OUTPUT_VARIABLE _otool_output
                    ERROR_QUIET)
                string(REPLACE \"\\n\" \";\" _otool_lines \"\${_otool_output}\")
                foreach(_otool_line IN LISTS _otool_lines)
                    string(STRIP \"\${_otool_line}\" _otool_line)
                    if(_otool_line MATCHES \"^([^ ]+) \")
                        set(_dependency_path \"\${CMAKE_MATCH_1}\")
                        get_filename_component(_dependency_name \"\${_dependency_path}\" NAME)
                        if(EXISTS \"\${_bundle_frameworks_dir}/\${_dependency_name}\")
                            set(_rewritten_dependency \"@executable_path/../Frameworks/\${_dependency_name}\")
                            if(NOT _dependency_path STREQUAL _rewritten_dependency)
                                execute_process(
                                    COMMAND \"${INSTALL_NAME_TOOL_EXECUTABLE}\"
                                            -change \"\${_dependency_path}\"
                                            \"\${_rewritten_dependency}\"
                                            \"\${_bundle_rewrite_item}\"
                                    RESULT_VARIABLE _install_name_change_result
                                    ERROR_QUIET)
                            endif()
                        endif()
                    endif()
                endforeach()
            endforeach()
            if(_unresolved_runtime_dependencies)
                message(WARNING \"Unresolved runtime dependencies for ${_target_output_name}.app: \${_unresolved_runtime_dependencies}\")
            endif()
        endif()
    ")
endfunction()

function(install_macos_codesign target)
    if(NOT MACOS_SIGN_APPS OR NOT MACOS_CODE_SIGN_IDENTITY)
        return()
    endif()
    if(NOT CODESIGN_EXECUTABLE)
        message(FATAL_ERROR "codesign not found")
    endif()

    get_target_property(_target_output_name ${target} OUTPUT_NAME)
    if(NOT _target_output_name)
        set(_target_output_name ${target})
    endif()
    if(NOT HOST_BUNDLE_INSTALL_DIR)
        set(HOST_BUNDLE_INSTALL_DIR ".")
    endif()

    set(_entitlements_args "")
    if(MACOS_CODE_SIGN_ENTITLEMENTS)
        set(_entitlements_args "--entitlements" "${MACOS_CODE_SIGN_ENTITLEMENTS}")
    endif()

    install(CODE "
        message(STATUS \"Signing ${_target_output_name}.app\")
        execute_process(
            COMMAND \"${CODESIGN_EXECUTABLE}\"
                    --force
                    --deep
                    --timestamp
                    --options runtime
                    ${_entitlements_args}
                    --sign \"${MACOS_CODE_SIGN_IDENTITY}\"
                    \"\${CMAKE_INSTALL_PREFIX}/${HOST_BUNDLE_INSTALL_DIR}/${_target_output_name}.app\"
            RESULT_VARIABLE _codesign_result)
        if(NOT _codesign_result EQUAL 0)
            message(FATAL_ERROR \"codesign failed for ${_target_output_name}.app\")
        endif()
    ")
endfunction()

mark_as_advanced(WINDEPLOYQT_EXECUTABLE MACDEPLOYQT_EXECUTABLE CODESIGN_EXECUTABLE INSTALL_NAME_TOOL_EXECUTABLE)
