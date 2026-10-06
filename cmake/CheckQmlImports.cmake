#[[
  @file   CheckQmlImports.cmake
  @brief  Fails the build when an app's QML needs a Qt module the Qt install lacks.

  @details An app's QML imports are resolved only at run time, and deployment
           (macdeployqt, windeployqt) silently skips a module it cannot find.
           A Qt install missing an add-on therefore builds and packages an app
           whose QML fails on launch. That is how the CI packages, built
           against aqt's base Qt, shipped qtcalibrate without Qt Quick 3D.

           For each directory in QML_DIRS this runs qmlimportscanner against
           the Qt QML directory and fails if

           - an imported module cannot be found, or
           - a QML plugin of a found module links a Qt library that is not
             installed. aqt installs add-ons without their dependencies, so
             QtQuick3D can be present while the QtShaderTools it links is not.

           The platform styles of Qt Quick Controls (Windows, macOS, iOS) are
           optional imports present only on their own platform, and are
           ignored.

           Run in script mode, normally from the POST_BUILD step that
           add_qml_import_check() in host/CMakeLists.txt attaches:

             cmake -DTARGET_NAME=<name> -DSCANNER=<qmlimportscanner>
                   -DQT_QML_DIR=<Qt qml dir> -DQT_LIB_DIR=<Qt lib or bin dir>
                   -DQML_DIRS=<dir>[|<dir>...] -P cmake/CheckQmlImports.cmake

  @param  TARGET_NAME  The app being checked; used in messages only.
  @param  SCANNER      Path of qmlimportscanner.
  @param  QT_QML_DIR   The Qt installation's qml directory.
  @param  QT_LIB_DIR   Where the Qt libraries are: lib on macOS (frameworks),
                       bin on Windows (DLLs).
  @param  QML_DIRS     The app's QML source directories, separated by "|".
  @param  SKIP_LIBRARY_CHECK  Optional; ON checks the imports only, for a
                       platform where no dependency tool was found.

  On Windows the caller must also pass CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM,
  _TOOL and _COMMAND, which an install script would get from CMake.
#]]

cmake_minimum_required(VERSION 3.20)

foreach(_var TARGET_NAME SCANNER QT_QML_DIR QT_LIB_DIR QML_DIRS)
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "CheckQmlImports.cmake: ${_var} is required")
    endif()
endforeach()

set(_ignored_modules
    QtQuick.Controls.Windows
    QtQuick.Controls.macOS
    QtQuick.Controls.iOS)

string(REPLACE "|" ";" _qml_dirs "${QML_DIRS}")

set(_missing_modules "")
set(_plugin_libraries "")
foreach(_qml_dir IN LISTS _qml_dirs)
    execute_process(
        COMMAND "${SCANNER}" -rootPath "${_qml_dir}" -importPath "${QT_QML_DIR}"
        OUTPUT_VARIABLE _json
        ERROR_VARIABLE _scanner_error
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${TARGET_NAME}: qmlimportscanner failed on ${_qml_dir}: ${_scanner_error}")
    endif()

    string(JSON _count LENGTH "${_json}")
    if(_count EQUAL 0)
        continue()
    endif()
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _type ERROR_VARIABLE _err GET "${_json}" ${_i} type)
        if(NOT _type STREQUAL "module")
            continue()
        endif()
        string(JSON _name GET "${_json}" ${_i} name)
        string(JSON _path ERROR_VARIABLE _err GET "${_json}" ${_i} path)
        if(_err OR _path STREQUAL "")
            if(NOT _name IN_LIST _ignored_modules)
                list(APPEND _missing_modules "${_name}")
            endif()
            continue()
        endif()
        string(JSON _plugin ERROR_VARIABLE _err GET "${_json}" ${_i} plugin)
        if(_err OR _plugin STREQUAL "")
            continue()
        endif()
        foreach(_candidate
                "${_path}/lib${_plugin}.dylib"
                "${_path}/lib${_plugin}.so"
                "${_path}/${_plugin}.dll")
            if(EXISTS "${_candidate}")
                list(APPEND _plugin_libraries "${_candidate}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(_missing_modules)
    list(REMOVE_DUPLICATES _missing_modules)
    list(JOIN _missing_modules "\n  " _missing_text)
    message(FATAL_ERROR
        "${TARGET_NAME}: its QML imports modules that are not in the Qt "
        "installation at ${QT_QML_DIR}:\n  ${_missing_text}\n"
        "The app would build and package, then fail when it loads its QML. "
        "Install the Qt add-on modules that provide them (with aqt or "
        "install-qt-action, list them under modules).")
endif()

if(_plugin_libraries AND NOT SKIP_LIBRARY_CHECK)
    list(REMOVE_DUPLICATES _plugin_libraries)
    file(GET_RUNTIME_DEPENDENCIES
        LIBRARIES ${_plugin_libraries}
        DIRECTORIES "${QT_LIB_DIR}"
        UNRESOLVED_DEPENDENCIES_VAR _unresolved
        RESOLVED_DEPENDENCIES_VAR _resolved
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES "^/usr/lib/" "^/System/" "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]")
    set(_missing_libraries "")
    foreach(_dependency IN LISTS _unresolved)
        # Only Qt's own libraries: anything else is the platform's concern.
        if(_dependency MATCHES "(^|[/@])(Qt[A-Za-z0-9]+)\\.framework/"
           OR _dependency MATCHES "^(Qt6[A-Za-z0-9]+)d?\\.dll$")
            list(APPEND _missing_libraries "${_dependency}")
        endif()
    endforeach()
    if(_missing_libraries)
        list(REMOVE_DUPLICATES _missing_libraries)
        list(JOIN _missing_libraries "\n  " _missing_text)
        message(FATAL_ERROR
            "${TARGET_NAME}: QML plugins it uses link Qt libraries that are not "
            "installed in ${QT_LIB_DIR}:\n  ${_missing_text}\n"
            "Install the Qt add-on modules that provide them; aqt does not "
            "install an add-on's dependencies.")
    endif()
endif()

list(LENGTH _plugin_libraries _plugin_count)
if(SKIP_LIBRARY_CHECK)
    message(STATUS "${TARGET_NAME}: QML imports resolved; plugin dependencies not checked")
else()
    message(STATUS "${TARGET_NAME}: QML imports resolved, ${_plugin_count} plugins' Qt dependencies present")
endif()
