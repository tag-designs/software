cmake_minimum_required(VERSION 3.15)

# set(CMAKE_BUILD_TYPE Debug)

if (APPLE)
#set(Qt6_DIR "$ENV{HOME}/Qt/6.8.2/macos/lib/cmake/Qt6")
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
set(HOST_BUNDLE_INSTALL_DIR tag_tools)

add_library(host_common INTERFACE)
target_include_directories(host_common INTERFACE "${CMAKE_CURRENT_LIST_DIR}/common")

add_subdirectory(libraries/tagcore)
if(BUILD_QT_APPS)
  add_subdirectory(libraries/sensoranalysis)
endif()
add_subdirectory(commandline)
if(BUILD_QT_APPS)
  add_subdirectory(libraries/qcustomplot)
  add_subdirectory(libraries/sensorui)
  add_subdirectory(applications/qtmon)
  add_subdirectory(applications/qtprogram)
  add_subdirectory(applications/qtcalibrate)
  add_subdirectory(applications/btdataviz)
  add_subdirectory(applications/sensorviz)
endif()

if(WIN32)
  set(HOST_WINDOWS_LIBDIR "${CMAKE_INSTALL_BINDIR}/lib")
  set(HOST_WINDOWS_APPDIR "${HOST_WINDOWS_LIBDIR}")
  set(HOST_WINDOWS_PLUGINDIR "${CMAKE_INSTALL_BINDIR}/plugins")
  set(HOST_WINDOWS_QMLDIR "${CMAKE_INSTALL_BINDIR}/qml")
  set(HOST_VCPKG_USES_STATIC_LIBS OFF)
  if(VCPKG_LIBRARY_LINKAGE STREQUAL "static" OR VCPKG_TARGET_TRIPLET MATCHES "static")
    set(HOST_VCPKG_USES_STATIC_LIBS ON)
  endif()

  add_executable(tag_tools_launcher_console common/windows_tool_launcher.cpp)
  add_executable(tag_tools_launcher_gui WIN32 common/windows_tool_launcher.cpp)
  target_compile_definitions(tag_tools_launcher_gui PRIVATE TAG_TOOLS_GUI_LAUNCHER)
  set_target_properties(
    tag_tools_launcher_console
    tag_tools_launcher_gui
    PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/launcher)

  function(install_windows_runtime_dependencies)
    set(runtime_targets ${ARGN})
    if(NOT runtime_targets)
      return()
    endif()

    set(runtime_executable_lines "")
    foreach(target_name IN LISTS runtime_targets)
      if(TARGET ${target_name})
        string(APPEND runtime_executable_lines "            \"$<TARGET_FILE:${target_name}>\"\n")
      endif()
    endforeach()
    if(runtime_executable_lines STREQUAL "")
      return()
    endif()

    set(runtime_directory_lines "")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET AND NOT HOST_VCPKG_USES_STATIC_LIBS)
      string(APPEND runtime_directory_lines "            \"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin\"\n")
      string(APPEND runtime_directory_lines "            \"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin\"\n")
    endif()
    if(TARGET Qt6::qmake)
      get_target_property(host_qmake_executable Qt6::qmake IMPORTED_LOCATION)
      get_filename_component(host_qt_bin_dir "${host_qmake_executable}" DIRECTORY)
      string(APPEND runtime_directory_lines "            \"${host_qt_bin_dir}\"\n")
    endif()
    set(runtime_directories_block "")
    if(NOT runtime_directory_lines STREQUAL "")
      string(APPEND runtime_directories_block "            DIRECTORIES\n${runtime_directory_lines}")
    endif()

    install(CODE "
        message(STATUS \"Collecting host runtime DLLs\")
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES
${runtime_executable_lines}
${runtime_directories_block}
            PRE_EXCLUDE_REGEXES
                \"api-ms-\"
                \"ext-ms-\"
            POST_EXCLUDE_REGEXES
                \"[Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss]ystem32[\\\\/].*\"
            RESOLVED_DEPENDENCIES_VAR host_runtime_deps
            UNRESOLVED_DEPENDENCIES_VAR host_unresolved_runtime_deps)
        if(host_runtime_deps)
            file(INSTALL
                DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${HOST_WINDOWS_LIBDIR}\"
                TYPE SHARED_LIBRARY
                FILES \${host_runtime_deps})
        endif()
        if(host_unresolved_runtime_deps)
            message(WARNING \"Unresolved host runtime dependencies: \${host_unresolved_runtime_deps}\")
        endif()
    ")

    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${HOST_WINDOWS_LIBDIR}")
    include(InstallRequiredSystemLibraries)
    foreach(runtime_lib ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
      install(FILES "${runtime_lib}" DESTINATION "${HOST_WINDOWS_LIBDIR}")
    endforeach()
  endfunction()
endif()

function(get_target_deploy_qml_dirs out_var target_name)
  get_target_property(deploy_qml_dirs ${target_name} QT_DEPLOY_QML_DIRS)
  if(NOT deploy_qml_dirs)
    get_target_property(deploy_qml_dirs ${target_name} WINDOWS_DEPLOY_QML_DIRS)
  endif()
  if(deploy_qml_dirs)
    set(${out_var} ${deploy_qml_dirs} PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

#[[
  @brief  Fails a target's build if its QML needs a Qt module that is missing.

  @details Adds a target, qml_import_check_<target>, that is part of the
           default build, runs after the app is built, and runs
           cmake/CheckQmlImports.cmake over the app's QT_DEPLOY_QML_DIRS. (A
           POST_BUILD step on the app itself would need to be added in the
           app's own directory.) QML imports are resolved only at
           run time and deployment skips what it cannot find, so without this
           a Qt install lacking an add-on (aqt's base install, for one) builds
           and packages an app that fails when it loads its QML.

           Locates Qt from Qt6_DIR, which in an installer or aqt layout is
           <qt>/lib/cmake/Qt6. Where qmlimportscanner is not found there, as
           with a vcpkg-built Qt, it warns and adds no check. A target with no
           QML directories gets no check.

  @param  target_name  An application target that may set QT_DEPLOY_QML_DIRS.
#]]
function(add_qml_import_check target_name)
  get_target_deploy_qml_dirs(qml_dirs ${target_name})
  if(NOT qml_dirs)
    return()
  endif()
  get_filename_component(qt_root "${Qt6_DIR}/../../.." ABSOLUTE)
  find_program(HOST_QMLIMPORTSCANNER_EXECUTABLE qmlimportscanner
    HINTS "${qt_root}/libexec" "${qt_root}/bin"
    NO_DEFAULT_PATH)
  if(NOT HOST_QMLIMPORTSCANNER_EXECUTABLE)
    message(WARNING "qmlimportscanner not found under ${qt_root}; "
                    "the QML imports of ${target_name} are not checked")
    return()
  endif()
  set(dependency_tool_args "")
  if(WIN32)
    set(qt_lib_dir "${qt_root}/bin")
    # An install script gets the dependency tool from CMake; a -P script must
    # be told. dumpbin sits beside cl.exe, which a build is not guaranteed to
    # have on PATH.
    get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(HOST_DUMPBIN_EXECUTABLE dumpbin HINTS "${compiler_dir}")
    if(HOST_DUMPBIN_EXECUTABLE)
      set(dependency_tool_args
          -DCMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM=windows+pe
          -DCMAKE_GET_RUNTIME_DEPENDENCIES_TOOL=dumpbin
          -DCMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND=${HOST_DUMPBIN_EXECUTABLE})
    else()
      message(WARNING "dumpbin not found; the Qt libraries ${target_name}'s "
                      "QML plugins link are not checked, only its imports")
      set(dependency_tool_args -DSKIP_LIBRARY_CHECK=ON)
    endif()
  else()
    set(qt_lib_dir "${qt_root}/lib")
  endif()
  list(JOIN qml_dirs "|" qml_dirs_arg)
  add_custom_target(
    qml_import_check_${target_name}
    ALL
    COMMAND ${CMAKE_COMMAND}
            -DTARGET_NAME=${target_name}
            -DSCANNER=${HOST_QMLIMPORTSCANNER_EXECUTABLE}
            -DQT_QML_DIR=${qt_root}/qml
            -DQT_LIB_DIR=${qt_lib_dir}
            -DQML_DIRS=${qml_dirs_arg}
            ${dependency_tool_args}
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckQmlImports.cmake
    VERBATIM)
  add_dependencies(qml_import_check_${target_name} ${target_name})
endfunction()

# What ships. The developer and bench programs -- tag-attach-cycle,
# tag-test-example, tag-monitor-test, tag-cal-write, tag-peek and
# qtmonitor-fixture-capture -- are built but deliberately not installed; see
# host/commandline/README.md. tag-test is a field tool and does ship despite
# the name.
set(host_cli_install_targets
    tag-dwnld
    tag-test
    tag-info
    tag-reset
    tag-start
    tag-stop
    tag-cal)
if(TARGET dataprocessing)
  list(APPEND host_cli_install_targets dataprocessing)
endif()
set(host_qt_install_targets btviz sensorviz qtmonitor qtprogram qtcalibrate)
set(host_qt_runtime_install_targets ${host_qt_install_targets})
if(TARGET dataprocessing)
  list(APPEND host_qt_runtime_install_targets dataprocessing)
endif()
set(host_install_targets)
set(host_candidate_install_targets ${host_qt_install_targets} ${host_cli_install_targets})
foreach(target_name ${host_candidate_install_targets})
  if(TARGET ${target_name})
    list(APPEND host_install_targets ${target_name})
  endif()
endforeach()

# Every shipped app whose QML the Qt install cannot fully satisfy fails to
# build, rather than packaging and failing at launch.
foreach(target_name ${host_qt_install_targets})
  if(TARGET ${target_name})
    add_qml_import_check(${target_name})
  endif()
endforeach()

if(APPLE)
  set(host_package_support_destination "${HOST_BUNDLE_INSTALL_DIR}")
elseif(WIN32)
  set(host_package_support_destination "${CMAKE_INSTALL_BINDIR}")
else()
  set(host_package_support_destination "${CMAKE_INSTALL_DATADIR}/${CPACK_PACKAGE_NAME}")
endif()

set(install_host_docs OFF)
if(BUILD_HOST_DOCS AND host_install_targets)
  set(install_host_docs ON)
endif()

if(host_install_targets)
  if(APPLE)
    foreach(target_name ${host_qt_runtime_install_targets})
      if(TARGET ${target_name})
        set_property(TARGET ${target_name} APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
      endif()
    endforeach()
  endif()

  if(WIN32 AND TARGET Qt6::qmake)
    get_target_property(host_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(host_qt_bin_dir "${host_qmake_executable}" DIRECTORY)
    set(host_windows_debugger_path "${host_qt_bin_dir}")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET AND NOT HOST_VCPKG_USES_STATIC_LIBS)
      string(APPEND host_windows_debugger_path "$<SEMICOLON>$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>")
    endif()
    string(APPEND host_windows_debugger_path "$<SEMICOLON>%PATH%")
    foreach(target_name ${host_qt_runtime_install_targets})
      if(TARGET ${target_name})
        set_property(
            TARGET ${target_name}
            PROPERTY VS_DEBUGGER_ENVIRONMENT "PATH=${host_windows_debugger_path}")
      endif()
    endforeach()
  endif()

  if(WIN32)
    install(
      TARGETS ${host_install_targets}
      RUNTIME DESTINATION ${HOST_WINDOWS_APPDIR})
    foreach(target_name ${host_qt_install_targets})
      if(TARGET ${target_name})
        install(
          PROGRAMS "$<TARGET_FILE:tag_tools_launcher_gui>"
          DESTINATION ${CMAKE_INSTALL_BINDIR}
          RENAME "${target_name}${CMAKE_EXECUTABLE_SUFFIX}")
      endif()
    endforeach()
    foreach(target_name ${host_cli_install_targets})
      if(TARGET ${target_name})
        install(
          PROGRAMS "$<TARGET_FILE:tag_tools_launcher_console>"
          DESTINATION ${CMAKE_INSTALL_BINDIR}
          RENAME "${target_name}${CMAKE_EXECUTABLE_SUFFIX}")
      endif()
    endforeach()
  else()
    install(
      TARGETS ${host_install_targets}
      BUNDLE DESTINATION ${HOST_BUNDLE_INSTALL_DIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()

  if(WIN32 OR APPLE)
    if(WIN32)
      install_windows_runtime_dependencies(${host_install_targets})
      set(has_qt_install_targets OFF)
      foreach(target_name ${host_qt_runtime_install_targets})
        if(TARGET ${target_name})
          set(has_qt_install_targets ON)
        endif()
      endforeach()
      if(has_qt_install_targets)
        include(DeployQt)
        foreach(target_name ${host_qt_runtime_install_targets})
          if(TARGET ${target_name})
            get_target_deploy_qml_dirs(deploy_qml_dirs ${target_name})
            if(deploy_qml_dirs)
              install_windeployqt(${target_name} ${deploy_qml_dirs})
            else()
              install_windeployqt(${target_name})
            endif()
          endif()
        endforeach()
      endif()
    elseif(APPLE)
      include(DeployQt)
      foreach(target_name ${host_qt_runtime_install_targets})
        if(NOT TARGET ${target_name})
          continue()
        endif()
        get_target_deploy_qml_dirs(deploy_qml_dirs ${target_name})
        if(deploy_qml_dirs)
          install_macdeployqt(${target_name} ${deploy_qml_dirs})
        else()
          install_macdeployqt(${target_name})
        endif()
        if(MACOS_DEPLOY_QT_PLUGINS_MANUALLY)
          # macdeployqt is run with -no-plugins in this mode, so copy only the
          # curated plugin set from DeployQt.cmake. This intentionally excludes
          # Qt SQL drivers.
          install_macos_qt_plugins(${target_name})
        endif()
        # macdeployqt copies whole QML directories; keep only what the app's
        # QML needs, before anything is signed.
        install_macos_prune_qml(${target_name} ${deploy_qml_dirs})
        install_macos_fixup_bundle(${target_name})
        if(NOT MACOS_USE_MACDEPLOYQT_DEPLOYMENT)
          install_macos_remove_bundle_signatures(${target_name})
          install_macos_codesign(${target_name})
        elseif(MACOS_DEPLOY_QT_PLUGINS_MANUALLY OR MACOS_CODE_SIGN_ENTITLEMENTS)
          install_macos_codesign(${target_name})
        endif()
      endforeach()

      # The command line tools are MACOSX_BUNDLE targets as well, and until
      # now nothing signed them: they shipped carrying only the ad-hoc
      # signature the linker applies to the inner Mach-O. That is a valid
      # signature for the executable, which is why running
      # tag-info.app/Contents/MacOS/tag-info from a terminal has always
      # worked, but a .app with no Contents/_CodeSignature/CodeResources
      # fails `codesign --verify` as a bundle. It would be refused if ever
      # double-clicked, and would fail notarization of the whole image.
      #
      # They need no deployment work -- no Qt, no bundled dylibs -- only the
      # seal, so this loop calls the signing step alone. dataprocessing
      # appears in both lists and is signed by the loop above.
      foreach(target_name ${host_cli_install_targets})
        if(NOT TARGET ${target_name})
          continue()
        endif()
        if(target_name IN_LIST host_qt_runtime_install_targets)
          continue()
        endif()
        install_macos_codesign(${target_name})
      endforeach()
    endif()
  endif()

  if(install_host_docs)
    install(SCRIPT "${HOST_DOCS_INSTALL_SCRIPT}")
    install(
      DIRECTORY "${HOST_DOCS_BUILD_DIR}/"
      DESTINATION "${host_package_support_destination}/docs")
  endif()

  include(BuildReport)
  install_build_report(
    DESTINATION "${host_package_support_destination}"
    TARGETS ${host_install_targets})
endif()
