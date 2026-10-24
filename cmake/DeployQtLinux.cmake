#[[
  @file   DeployQtLinux.cmake
  @brief  Makes an installed Linux tag_tools tree self-contained: Qt's
          libraries, a chosen set of its plugins, and the QML modules the
          applications import.

  @details The Linux counterpart of windeployqt and macdeployqt, which have no
           Linux equivalent in Qt. Run at install time, after the executables
           are installed to <PREFIX>/bin with an RPATH of $ORIGIN/../lib, it

           1. copies the QML modules the applications' QML imports, as
              qmlimportscanner reports them, into <PREFIX>/qml -- each module's
              own files only, never a nested module it was not asked for, so
              the GPL-only modules a full Qt carries under QtQuick (Virtual
              Keyboard, for one) are never copied;
           2. copies the plugins in the list below into <PREFIX>/plugins;
           3. copies every library the executables, the plugins and the QML
              plugins need that is not a system library into <PREFIX>/lib,
              resolving them against the Qt installation;
           4. writes <PREFIX>/bin/qt.conf, pointing Qt at those directories.

           The tree mirrors a Qt installation's own layout (lib, plugins, qml
           side by side), so the RUNPATHs Qt's own libraries and plugins carry
           ($ORIGIN, $ORIGIN/../../lib, ...) resolve inside it unchanged.

           System libraries -- glibc, the display stack, OpenGL, fontconfig --
           are left to the system. A library that cannot be found at all
           fails the install if it is Qt's own or ICU, and is reported
           otherwise.

           Script mode:

             cmake -DPREFIX=<tag_tools dir> -DSCANNER=<qmlimportscanner>
                   -DQT_LIB_DIR=<dir> -DQT_PLUGINS_DIR=<dir> -DQT_QML_DIR=<dir>
                   -DQML_DIRS=<dir>[|<dir>...]
                   -P cmake/DeployQtLinux.cmake

  @param  PREFIX          The installed tag_tools directory.
  @param  SCANNER         Path of qmlimportscanner.
  @param  QT_LIB_DIR      The Qt installation's library directory.
  @param  QT_PLUGINS_DIR  The Qt installation's plugin directory.
  @param  QT_QML_DIR      The Qt installation's QML directory.
  @param  QML_DIRS        The applications' QML source directories, separated
                          by "|"; may be empty.
#]]

cmake_minimum_required(VERSION 3.20)

foreach(_var PREFIX SCANNER QT_LIB_DIR QT_PLUGINS_DIR QT_QML_DIR)
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "DeployQtLinux.cmake: ${_var} is required")
    endif()
endforeach()

# The plugins the applications load. Qt picks the platform plugin at start-up
# (wayland on a Wayland session, xcb on X11), and nothing links any of these,
# so they are named rather than discovered. On Wayland, GNOME leaves window
# decorations to the client (wayland-decoration-client), EGL rendering needs
# the wayland-egl integration, and xdg-shell is the shell desktops speak.
# Server-buffer integrations (*-server) are left out: they are for embedded
# compositors, not desktops. qtprogram lists and downloads firmware releases
# over HTTPS, which needs a TLS backend: the OpenSSL one loads the system's
# libssl at run time (so ldd shows no dependency), and the certificate-only one
# is Qt's fallback.
set(_plugin_globs
    platforms/libqxcb.so
    platforms/libqwayland*.so
    xcbglintegrations/*.so
    wayland-decoration-client/*.so
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
    wayland-shell-integration/libxdg-shell.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
    platforminputcontexts/libibusplatforminputcontextplugin.so
    platformthemes/libqxdgdesktopportal.so
    printsupport/libcupsprintersupport.so
    imageformats/*.so
    iconengines/*.so
    tls/*.so)

# --- 1. QML modules -----------------------------------------------------------

set(_qml_out "${PREFIX}/qml")
string(REPLACE "|" ";" _qml_dirs "${QML_DIRS}")
set(_modules "")
foreach(_qml_dir IN LISTS _qml_dirs)
    execute_process(
        COMMAND "${SCANNER}" -rootPath "${_qml_dir}" -importPath "${QT_QML_DIR}"
        OUTPUT_VARIABLE _json
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "qmlimportscanner failed on ${_qml_dir}")
    endif()
    string(JSON _count LENGTH "${_json}")
    if(_count EQUAL 0)
        continue()
    endif()
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _rel ERROR_VARIABLE _err GET "${_json}" ${_i} relativePath)
        if(NOT _err AND NOT _rel STREQUAL "")
            list(APPEND _modules "${_rel}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _modules)

# A file belongs to the module whose qmldir is its nearest ancestor. Copy a
# module's files and its non-module subdirectories (images, implementation
# QML), but stop at any subdirectory that is a module in its own right.
set(_qml_plugins "")
foreach(_module IN LISTS _modules)
    set(_src "${QT_QML_DIR}/${_module}")
    if(NOT EXISTS "${_src}/qmldir")
        message(FATAL_ERROR "QML module ${_module} has no qmldir under ${QT_QML_DIR}")
    endif()
    file(GLOB_RECURSE _files LIST_DIRECTORIES false RELATIVE "${_src}" "${_src}/*")
    foreach(_file IN LISTS _files)
        get_filename_component(_dir "${_file}" DIRECTORY)
        set(_owned TRUE)
        while(NOT _dir STREQUAL "")
            if(EXISTS "${_src}/${_dir}/qmldir")
                set(_owned FALSE)
                break()
            endif()
            get_filename_component(_dir "${_dir}" DIRECTORY)
        endwhile()
        if(NOT _owned)
            continue()
        endif()
        get_filename_component(_dest_dir "${_qml_out}/${_module}/${_file}" DIRECTORY)
        file(COPY "${_src}/${_file}" DESTINATION "${_dest_dir}")
        if(_file MATCHES "\\.so$")
            list(APPEND _qml_plugins "${_qml_out}/${_module}/${_file}")
        endif()
    endforeach()
endforeach()
list(LENGTH _modules _module_count)

# --- 2. Plugins ---------------------------------------------------------------

set(_plugins "")
foreach(_glob IN LISTS _plugin_globs)
    file(GLOB _found "${QT_PLUGINS_DIR}/${_glob}")
    foreach(_plugin IN LISTS _found)
        get_filename_component(_type_dir "${_plugin}" DIRECTORY)
        get_filename_component(_type "${_type_dir}" NAME)
        file(COPY "${_plugin}" DESTINATION "${PREFIX}/plugins/${_type}")
        get_filename_component(_name "${_plugin}" NAME)
        list(APPEND _plugins "${PREFIX}/plugins/${_type}/${_name}")
    endforeach()
endforeach()
if(NOT EXISTS "${PREFIX}/plugins/tls/libqopensslbackend.so")
    message(FATAL_ERROR "the OpenSSL TLS plugin is not in ${QT_PLUGINS_DIR}")
endif()
if(NOT EXISTS "${PREFIX}/plugins/platforms/libqxcb.so")
    message(FATAL_ERROR "the xcb platform plugin is not in ${QT_PLUGINS_DIR}")
endif()
file(GLOB _wayland_platform "${PREFIX}/plugins/platforms/libqwayland*.so")
if(NOT _wayland_platform)
    message(FATAL_ERROR "the wayland platform plugin is not in ${QT_PLUGINS_DIR}")
endif()
list(LENGTH _plugins _plugin_count)

# --- 3. Libraries -------------------------------------------------------------

file(GLOB _bin_files LIST_DIRECTORIES false "${PREFIX}/bin/*")
set(_executables "")
foreach(_file IN LISTS _bin_files)
    if(IS_SYMLINK "${_file}" OR _file MATCHES "\\.conf$")
        continue()
    endif()
    file(READ "${_file}" _magic LIMIT 4 HEX)
    if(_magic STREQUAL "7f454c46")
        list(APPEND _executables "${_file}")
    endif()
endforeach()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${_executables}
    MODULES ${_plugins} ${_qml_plugins}
    DIRECTORIES "${QT_LIB_DIR}"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    POST_EXCLUDE_REGEXES "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/")
file(INSTALL DESTINATION "${PREFIX}/lib" TYPE SHARED_LIBRARY
    FOLLOW_SYMLINK_CHAIN FILES ${_resolved})
list(LENGTH _resolved _library_count)

set(_missing_qt "")
set(_missing_other "")
foreach(_dependency IN LISTS _unresolved)
    if(_dependency MATCHES "^lib(Qt6|icu)")
        list(APPEND _missing_qt "${_dependency}")
    else()
        list(APPEND _missing_other "${_dependency}")
    endif()
endforeach()
if(_missing_qt)
    list(JOIN _missing_qt " " _text)
    message(FATAL_ERROR "Qt libraries not found in ${QT_LIB_DIR}: ${_text}")
endif()
if(_missing_other)
    list(JOIN _missing_other " " _text)
    message(STATUS "left to the system, not installed on this machine: ${_text}")
endif()

# --- 4. qt.conf -----------------------------------------------------------------

file(WRITE "${PREFIX}/bin/qt.conf"
"[Paths]
Prefix = ..
Libraries = lib
Plugins = plugins
QmlImports = qml
")

message(STATUS "Deployed Qt into ${PREFIX}: ${_library_count} libraries, "
               "${_plugin_count} plugins, ${_module_count} QML modules")
