cmake_minimum_required(VERSION 3.15)

# set(CMAKE_BUILD_TYPE Debug)

if (APPLE)
#set(Qt6_DIR "$ENV{HOME}/Qt/6.8.2/macos/lib/cmake/Qt6")
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
set(HOST_BUNDLE_INSTALL_DIR UltralightTags)

add_subdirectory(lib)
add_subdirectory(cli)
if(BUILD_QT_APPS)
  add_subdirectory(qcustomplot)
  add_subdirectory(qtmon)
  add_subdirectory(qtprogram)
  add_subdirectory(qtcalibrate)
  add_subdirectory(btdataviz)
  add_subdirectory(compviz)
  add_subdirectory(tag-orientation)
endif()

if(WIN32)
  set(HOST_WINDOWS_APPDIR "${CMAKE_INSTALL_BINDIR}/apps")
  set(HOST_WINDOWS_LIBDIR "${CMAKE_INSTALL_BINDIR}/lib")
  set(HOST_WINDOWS_PLUGINDIR "${CMAKE_INSTALL_BINDIR}/plugins")
  set(HOST_WINDOWS_QMLDIR "${CMAKE_INSTALL_BINDIR}/qml")

  add_executable(tag_tools_launcher_console windows_tool_launcher.cpp)
  add_executable(tag_tools_launcher_gui WIN32 windows_tool_launcher.cpp)
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
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
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

set(host_cli_install_targets
    tag-dwnld
    tag-test
    tag-test-example
    tag-info
    tag-reset
    tag-start
    tag-cal
    tag-monitor-test)
set(host_qt_install_targets btviz compviz qtmonitor qtprogram qtcalibrate)
set(host_install_targets)
if(WIN32)
  set(host_candidate_install_targets ${host_cli_install_targets} ${host_qt_install_targets})
else()
  set(host_candidate_install_targets ${host_qt_install_targets})
endif()
foreach(target_name ${host_candidate_install_targets})
  if(TARGET ${target_name})
    list(APPEND host_install_targets ${target_name})
  endif()
endforeach()

if(host_install_targets)
  if(APPLE)
    foreach(target_name ${host_qt_install_targets})
      if(TARGET ${target_name})
        set_property(TARGET ${target_name} APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
      endif()
    endforeach()
  endif()

  if(WIN32 AND TARGET Qt6::qmake)
    get_target_property(host_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(host_qt_bin_dir "${host_qmake_executable}" DIRECTORY)
    set(host_windows_debugger_path "${host_qt_bin_dir}")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
      string(APPEND host_windows_debugger_path "$<SEMICOLON>$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>")
    endif()
    string(APPEND host_windows_debugger_path "$<SEMICOLON>%PATH%")
    foreach(target_name ${host_qt_install_targets})
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
    foreach(target_name ${host_cli_install_targets})
      if(TARGET ${target_name})
        install(
          PROGRAMS "$<TARGET_FILE:tag_tools_launcher_console>"
          DESTINATION ${CMAKE_INSTALL_BINDIR}
          RENAME "${target_name}${CMAKE_EXECUTABLE_SUFFIX}")
      endif()
    endforeach()
    foreach(target_name ${host_qt_install_targets})
      if(TARGET ${target_name})
        install(
          PROGRAMS "$<TARGET_FILE:tag_tools_launcher_gui>"
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
      foreach(target_name ${host_qt_install_targets})
        if(TARGET ${target_name})
          set(has_qt_install_targets ON)
        endif()
      endforeach()
      if(has_qt_install_targets)
        include(DeployQt)
        foreach(target_name ${host_qt_install_targets})
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
      foreach(target_name ${host_qt_install_targets})
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
        if(NOT MACOS_USE_MACDEPLOYQT_DEPLOYMENT)
          install_macos_remove_bundle_signatures(${target_name})
          install_macos_fixup_bundle(${target_name})
          install_macos_codesign(${target_name})
        endif()
      endforeach()
    endif()
  endif()
endif()
