#[[
  @file   PruneMacosQml.cmake
  @brief  Removes QML modules and Qt frameworks an app bundle does not use.

  @details macdeployqt deploys every module installed under each QML directory
           an app imports, not only the modules the app's QML needs. With a full
           Qt installation that puts Qt Virtual Keyboard, Qt Quick 3D Physics,
           Xr and SpatialAudio, Multimedia and more into qtcalibrate.app: dead
           weight, and several of them licensed only under GPL-3.0.

           Run after macdeployqt and before signing, this

           1. asks qmlimportscanner which QML modules the app's QML needs,
              transitively, in the Qt installation (the same question
              CheckQmlImports.cmake asks at build time);
           2. removes every module directory under Contents/Resources/qml that
              is neither needed nor a parent of a needed one, and the plugins in
              Contents/PlugIns/quick that only removed modules linked to;
           3. removes every Qt framework in Contents/Frameworks that nothing
              left in the bundle links to, following otool -L from the
              executables and every remaining plugin.

           It removes nothing a remaining binary links to, so a mistake shows up
           as a module missing at run time rather than a broken link. The QML
           import check guards the other direction.

           Script mode:

             cmake -DBUNDLE=<path.app> -DSCANNER=<qmlimportscanner>
                   -DQT_QML_DIR=<Qt qml dir> -DQML_DIRS=<dir>[|<dir>...]
                   -P cmake/PruneMacosQml.cmake

  @param  BUNDLE       The deployed .app bundle.
  @param  SCANNER      Path of qmlimportscanner.
  @param  QT_QML_DIR   The Qt installation's qml directory.
  @param  QML_DIRS     The app's QML source directories, separated by "|".
#]]

cmake_minimum_required(VERSION 3.20)

foreach(_var BUNDLE SCANNER QT_QML_DIR QML_DIRS)
    if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
        message(FATAL_ERROR "PruneMacosQml.cmake: ${_var} is required")
    endif()
endforeach()

get_filename_component(_app_name "${BUNDLE}" NAME)
set(_qml_root "${BUNDLE}/Contents/Resources/qml")
set(_quick_plugins "${BUNDLE}/Contents/PlugIns/quick")
set(_frameworks "${BUNDLE}/Contents/Frameworks")
if(NOT IS_DIRECTORY "${_qml_root}")
    message(STATUS "${_app_name}: no deployed QML, nothing to prune")
    return()
endif()

# 1. The QML modules the app needs, as paths relative to the qml directory.
string(REPLACE "|" ";" _qml_dirs "${QML_DIRS}")
set(_needed "")
foreach(_qml_dir IN LISTS _qml_dirs)
    execute_process(
        COMMAND "${SCANNER}" -rootPath "${_qml_dir}" -importPath "${QT_QML_DIR}"
        OUTPUT_VARIABLE _json
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_app_name}: qmlimportscanner failed on ${_qml_dir}")
    endif()
    string(JSON _count LENGTH "${_json}")
    if(_count EQUAL 0)
        continue()
    endif()
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _rel ERROR_VARIABLE _err GET "${_json}" ${_i} relativePath)
        if(NOT _err AND NOT _rel STREQUAL "")
            list(APPEND _needed "${_rel}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _needed)
if(NOT _needed)
    message(FATAL_ERROR "${_app_name}: qmlimportscanner reported no QML modules; refusing to prune")
endif()

# 2. Remove module directories that are not needed and hold nothing needed.
file(GLOB_RECURSE _qmldir_files LIST_DIRECTORIES false "${_qml_root}/*/qmldir")
set(_removed_modules "")
foreach(_qmldir IN LISTS _qmldir_files)
    get_filename_component(_dir "${_qmldir}" DIRECTORY)
    file(RELATIVE_PATH _rel "${_qml_root}" "${_dir}")
    if(_rel IN_LIST _needed)
        continue()
    endif()
    set(_holds_needed FALSE)
    foreach(_n IN LISTS _needed)
        string(FIND "${_n}" "${_rel}/" _pos)
        if(_pos EQUAL 0)
            set(_holds_needed TRUE)
            break()
        endif()
    endforeach()
    if(NOT _holds_needed AND EXISTS "${_dir}")
        file(REMOVE_RECURSE "${_dir}")
        list(APPEND _removed_modules "${_rel}")
    endif()
endforeach()

# Plugins in PlugIns/quick that no remaining module links to.
file(GLOB_RECURSE _qml_links "${_qml_root}/*.dylib")
set(_linked_plugins "")
foreach(_link IN LISTS _qml_links)
    if(IS_SYMLINK "${_link}")
        file(READ_SYMLINK "${_link}" _target)
        get_filename_component(_target_name "${_target}" NAME)
        list(APPEND _linked_plugins "${_target_name}")
    endif()
endforeach()
file(GLOB _quick_plugin_files "${_quick_plugins}/*.dylib")
foreach(_plugin IN LISTS _quick_plugin_files)
    get_filename_component(_plugin_name "${_plugin}" NAME)
    if(NOT _plugin_name IN_LIST _linked_plugins)
        file(REMOVE "${_plugin}")
    endif()
endforeach()

# 3. Keep only the Qt frameworks something left in the bundle links to.
file(GLOB _executables LIST_DIRECTORIES false "${BUNDLE}/Contents/MacOS/*")
file(GLOB_RECURSE _plugins LIST_DIRECTORIES false "${BUNDLE}/Contents/PlugIns/*.dylib")
file(GLOB_RECURSE _qml_plugins LIST_DIRECTORIES false "${_qml_root}/*.dylib")
set(_queue ${_executables} ${_plugins} ${_qml_plugins})
set(_seen "")
set(_keep "")
while(_queue)
    list(POP_FRONT _queue _binary)
    if(_binary IN_LIST _seen OR NOT EXISTS "${_binary}")
        continue()
    endif()
    list(APPEND _seen "${_binary}")
    execute_process(COMMAND otool -L "${_binary}" OUTPUT_VARIABLE _deps RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        continue()
    endif()
    string(REGEX MATCHALL "/(Qt[A-Za-z0-9]+)\\.framework/" _matches "${_deps}")
    foreach(_m IN LISTS _matches)
        string(REGEX REPLACE "^/(Qt[A-Za-z0-9]+)\\.framework/$" "\\1" _fw "${_m}")
        if(NOT _fw IN_LIST _keep)
            list(APPEND _keep "${_fw}")
            list(APPEND _queue "${_frameworks}/${_fw}.framework/Versions/A/${_fw}")
        endif()
    endforeach()
endwhile()

file(GLOB _framework_dirs LIST_DIRECTORIES true "${_frameworks}/Qt*.framework")
set(_removed_frameworks "")
foreach(_fw_dir IN LISTS _framework_dirs)
    get_filename_component(_fw "${_fw_dir}" NAME_WE)
    if(NOT _fw IN_LIST _keep)
        file(REMOVE_RECURSE "${_fw_dir}")
        list(APPEND _removed_frameworks "${_fw}")
    endif()
endforeach()

list(LENGTH _removed_modules _module_count)
list(LENGTH _removed_frameworks _framework_count)
list(JOIN _removed_frameworks " " _removed_text)
message(STATUS "${_app_name}: pruned ${_module_count} unused QML modules and "
               "${_framework_count} unused Qt frameworks: ${_removed_text}")
