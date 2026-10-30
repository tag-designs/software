cmake_minimum_required(VERSION 3.15)

# set(CMAKE_BUILD_TYPE Debug)

if (APPLE)
#set(Qt6_DIR "$ENV{HOME}/Qt/6.8.2/macos/lib/cmake/Qt6")
endif()

# On Linux the installed tree is relocatable -- executables find their
# libraries through $ORIGIN/../lib, set below -- so an installed executable
# must not also carry the build machine's library directories.
if(APPLE OR WIN32)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
set(HOST_BUNDLE_INSTALL_DIR tag_tools)

# host_common: header-only helpers shared by the Qt applications, in
# host/common (qtuserguide.h, qtfiledialog.h, qthoststyle.h). Nothing is
# compiled here; linking it adds the include path and the two definitions
# below. sensorviz also uses TAG_DESIGNS_DOCS_SOURCE_DIR as its default
# directory for documentation screenshots.
add_library(host_common INTERFACE)
target_include_directories(host_common INTERFACE "${CMAKE_CURRENT_LIST_DIR}/common")
# Where qtuserguide.h looks for the built user manual, after the places beside
# the application: this build's site, then a site built in the source tree.
target_compile_definitions(host_common INTERFACE
  TAG_DESIGNS_DOCS_BUILD_DIR="${HOST_DOCS_BUILD_DIR}"
  TAG_DESIGNS_DOCS_SOURCE_DIR="${HOST_DOCS_SOURCE_DIR}")

add_subdirectory(libraries/tagcore)
# Always added: its Qt-free core is what dataprocessing links. The Qt library
# in it is built only with the Qt applications.
add_subdirectory(libraries/sensoranalysis)
# Qt-free, needs only sqlite3; sensorviz plots through it.
add_subdirectory(libraries/sensordata)
# sensorViz's Qt-free core, beside its Qt front end but built without Qt: the
# Qt application and the C interface both link it. It reads calibration
# through sensoranalysis_core_json, which exists when protobuf was found.
if(TARGET sensoranalysis_core_json)
  add_subdirectory(applications/sensorviz/core)
endif()
# The C interface for Rust and other languages: one shared library exporting
# only C functions. Tag Monitor in Rust writes its downloads through it, so it
# is built whenever the Rust applications are; otherwise only when asked for.
option(BUILD_TAGCORE_C "Build the tagcore_c shared library (C interface for Rust tools)" OFF)
if(BUILD_TAGCORE_C OR BUILD_RUST_APPS)
  add_subdirectory(libraries/tagcore-c)
endif()
add_subdirectory(commandline)
if(BUILD_TAG_SERVER)
  add_subdirectory(tagserver)
endif()
if(BUILD_QT_APPS)
  add_subdirectory(libraries/qcustomplot)
  add_subdirectory(libraries/sensorui)
  add_subdirectory(applications/qtmon)
  add_subdirectory(applications/qtprogram)
  add_subdirectory(applications/qtcalibrate)
  add_subdirectory(applications/btdataviz)
  add_subdirectory(applications/sensorviz)
endif()

# The applications build against whatever Qt is found; HOST_QT_VERSION (see
# the top-level CMakeLists.txt) says whether it is the one releases use.
if(BUILD_QT_APPS AND HOST_QT_VERSION)
  find_package(Qt6 QUIET COMPONENTS Core)
  if(Qt6Core_VERSION AND NOT Qt6Core_VERSION VERSION_EQUAL HOST_QT_VERSION)
    reproducibility_problem(
      "The host applications are building against Qt ${Qt6Core_VERSION} "
      "(${Qt6_DIR}), but releases are built with Qt ${HOST_QT_VERSION}. They "
      "will build and run, but not as released. Install Qt ${HOST_QT_VERSION} "
      "and point CMAKE_PREFIX_PATH at it, or configure with -DHOST_QT_VERSION= "
      "to skip this check.")
  endif()
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
  @brief  Finds the Qt installation's directories and qmlimportscanner.

  @details Takes them from the install layout Qt records for itself
           (QT6_INSTALL_PREFIX and the QT6_INSTALL_* directories), not from
           paths inferred from Qt6_DIR. An installer or aqt Qt keeps
           everything under one root, but a distribution Qt splits it:
           Debian, for one, puts qmlimportscanner in /usr/lib/qt6/libexec and
           the QML in /usr/lib/<arch>/qt6/qml.

           Sets, in the caller's scope, qt_qml_dir, qt_libexecs_dir,
           qt_bins_dir, qt_libs_dir and qt_plugins_dir, and the cache entry
           HOST_QMLIMPORTSCANNER_EXECUTABLE.

  @param  out_var  Set to TRUE when the layout is known and qmlimportscanner
                   was found in Qt's libexec or bin directory, FALSE (with the
                   reason in ${out_var}_REASON) otherwise.
#]]
function(host_find_qt_layout out_var)
  set(${out_var} FALSE PARENT_SCOPE)
  # The apps find Qt in their own directories, so its variables are not set
  # in this one; finding it again here defines QT6_INSTALL_*.
  find_package(Qt6 QUIET COMPONENTS Core)
  if(NOT QT6_INSTALL_PREFIX)
    set(${out_var}_REASON "Qt's install layout is unknown" PARENT_SCOPE)
    return()
  endif()
  # The QT6_INSTALL_* directories are relative to the prefix, but accept an
  # absolute one rather than assume every packager follows that.
  foreach(kind QML LIBEXECS BINS LIBS PLUGINS)
    set(dir "${QT6_INSTALL_${kind}}")
    if(NOT IS_ABSOLUTE "${dir}")
      set(dir "${QT6_INSTALL_PREFIX}/${dir}")
    endif()
    string(TOLOWER "qt_${kind}_dir" dir_var)
    set(${dir_var} "${dir}" PARENT_SCOPE)
    set(${dir_var} "${dir}")
  endforeach()
  find_program(HOST_QMLIMPORTSCANNER_EXECUTABLE qmlimportscanner
    HINTS "${qt_libexecs_dir}" "${qt_bins_dir}"
    NO_DEFAULT_PATH)
  if(NOT HOST_QMLIMPORTSCANNER_EXECUTABLE)
    set(${out_var}_REASON
        "qmlimportscanner not found in ${qt_libexecs_dir} or ${qt_bins_dir}"
        PARENT_SCOPE)
    return()
  endif()
  set(${out_var} TRUE PARENT_SCOPE)
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

           Locates Qt with host_find_qt_layout(). Where that fails, it warns
           and adds no check. A target with no QML directories gets no check.

  @param  target_name  An application target that may set QT_DEPLOY_QML_DIRS.
#]]
function(add_qml_import_check target_name)
  get_target_deploy_qml_dirs(qml_dirs ${target_name})
  if(NOT qml_dirs)
    return()
  endif()
  host_find_qt_layout(qt_found)
  if(NOT qt_found)
    message(WARNING "${qt_found_REASON}; "
                    "the QML imports of ${target_name} are not checked")
    return()
  endif()
  set(dependency_tool_args "")
  if(WIN32)
    set(qt_lib_dir "${qt_bins_dir}")
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
    set(qt_lib_dir "${qt_libs_dir}")
  endif()
  list(JOIN qml_dirs "|" qml_dirs_arg)
  add_custom_target(
    qml_import_check_${target_name}
    ALL
    COMMAND ${CMAKE_COMMAND}
            -DTARGET_NAME=${target_name}
            -DSCANNER=${HOST_QMLIMPORTSCANNER_EXECUTABLE}
            -DQT_QML_DIR=${qt_qml_dir}
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
    tag-cal
    tag-txt-import)
if(TARGET dataprocessing)
  list(APPEND host_cli_install_targets dataprocessing)
endif()
set(host_qt_install_targets btviz sensorviz qtmonitor qtprogram qtcalibrate)
# The targets that need Qt deployed beside them. Every command line tool,
# dataprocessing included, is a plain executable that links no Qt.
set(host_qt_runtime_install_targets ${host_qt_install_targets})
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

if(WIN32)
  set(host_package_support_destination "${CMAKE_INSTALL_BINDIR}")
else()
  set(host_package_support_destination "${HOST_BUNDLE_INSTALL_DIR}")
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
    # The plain executables go to tag_tools/bin, so the package keeps one
    # top-level folder and the tools one directory to put on PATH: on macOS
    # the command line tools, inside the folder the user drags out of the
    # DMG; on Linux every executable, the Qt applications included.
    set(host_cli_runtime_destination "${HOST_BUNDLE_INSTALL_DIR}/bin")
    if(NOT APPLE)
      foreach(target_name ${host_install_targets})
        set_property(TARGET ${target_name} PROPERTY INSTALL_RPATH "$ORIGIN/../lib")
      endforeach()
    endif()
    install(
      TARGETS ${host_install_targets}
      BUNDLE DESTINATION ${HOST_BUNDLE_INSTALL_DIR}
      RUNTIME DESTINATION ${host_cli_runtime_destination})
  endif()

  if(UNIX AND NOT APPLE)
    # Qt's libraries, plugins and the QML the applications import, copied
    # into tag_tools by cmake/DeployQtLinux.cmake; see that file.
    set(linux_qml_dirs "")
    set(linux_has_qt_targets OFF)
    foreach(target_name ${host_qt_runtime_install_targets})
      if(TARGET ${target_name})
        set(linux_has_qt_targets ON)
        get_target_deploy_qml_dirs(deploy_qml_dirs ${target_name})
        list(APPEND linux_qml_dirs ${deploy_qml_dirs})
      endif()
    endforeach()
    if(linux_has_qt_targets)
      host_find_qt_layout(qt_found)
    endif()
    if(linux_has_qt_targets AND NOT qt_found)
      # A developer install against a Qt without its tools still installs;
      # it just runs only where that Qt is.
      message(WARNING "${qt_found_REASON}; the Linux install will not carry Qt "
                      "and runs only on a machine with this Qt installed")
    elseif(linux_has_qt_targets AND qt_libs_dir MATCHES "^/usr/")
      # A distribution's Qt is the system's: the install uses it in place.
      # Copying its plugins and pointing a qt.conf at the copies would cut the
      # applications off from the plugins the distribution adds (its platform
      # theme, for one).
      message(STATUS "Qt is the system's (${qt_libs_dir}); the Linux install "
                     "uses it in place rather than carrying a copy")
    elseif(linux_has_qt_targets)
      list(REMOVE_DUPLICATES linux_qml_dirs)
      list(JOIN linux_qml_dirs "|" linux_qml_dirs_arg)
      install(CODE "
        execute_process(
          COMMAND \"${CMAKE_COMMAND}\"
                  \"-DPREFIX=\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${HOST_BUNDLE_INSTALL_DIR}\"
                  \"-DSCANNER=${HOST_QMLIMPORTSCANNER_EXECUTABLE}\"
                  \"-DQT_LIB_DIR=${qt_libs_dir}\"
                  \"-DQT_PLUGINS_DIR=${qt_plugins_dir}\"
                  \"-DQT_QML_DIR=${qt_qml_dir}\"
                  \"-DQML_DIRS=${linux_qml_dirs_arg}\"
                  -P \"${PROJECT_SOURCE_DIR}/cmake/DeployQtLinux.cmake\"
          RESULT_VARIABLE _deploy_result)
        if(NOT _deploy_result EQUAL 0)
          message(FATAL_ERROR \"Qt deployment into ${HOST_BUNDLE_INSTALL_DIR} failed\")
        endif()
      ")
    endif()

    # Lets an ordinary user open the tag base; see the file.
    install(
      FILES "${CMAKE_CURRENT_SOURCE_DIR}/linux/70-ultralight-tags.rules"
      DESTINATION "${HOST_BUNDLE_INSTALL_DIR}/udev")
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

      # The command line tools are plain executables with their libraries
      # linked statically, so they need no deployment, only a signature: the
      # linker's ad-hoc one would fail notarization of the whole image.
      foreach(target_name ${host_cli_install_targets})
        if(NOT TARGET ${target_name})
          continue()
        endif()
        install_macos_codesign_executable(${target_name} "${host_cli_runtime_destination}")
      endforeach()
    endif()
  endif()

  if(install_host_docs)
    install(SCRIPT "${HOST_DOCS_INSTALL_SCRIPT}")
    install(
      DIRECTORY "${HOST_DOCS_BUILD_DIR}/"
      DESTINATION "${host_package_support_destination}/docs")
  endif()

  # The license index, the MIT License, every third-party notice and the
  # license texts they name (see LICENSES/README.md). The directory is
  # self-contained, so it is installed as it stands.
  install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/LICENSES/host/"
    DESTINATION "${host_package_support_destination}/licenses")

  include(BuildReport)
  install_build_report(
    DESTINATION "${host_package_support_destination}"
    TARGETS ${host_install_targets})
endif()

# The Rust applications: Tag Programmer, Tag Monitor, Tag Calibrator and Tag
# View (sensorViz in Rust), and the Rust programming tools. Added last so their install rules run after the
# Qt deployment steps above, which scan the installed executables.
if(BUILD_RUST_APPS)
  add_subdirectory(applications/tagprogram-rs)
  add_subdirectory(applications/tagmonitor-rs)
  add_subdirectory(applications/tagcalibrate-rs)
  add_subdirectory(applications/sensorviz-rs)
  # tag-flash and base-flash, which program tags and bases without
  # STM32CubeProgrammer.
  add_subdirectory(commandline-rs)
  # The shared tagcore_c that Tag Monitor, Tag Calibrator and Tag View link
  # on Linux and Windows, installed once: beside the programs in
  # tag_tools/lib on Windows, where a program finds its DLLs only beside it;
  # in tag_tools/lib on Linux, where their rpath points. macOS links the
  # static archive into each executable.
  if(WIN32)
    install(TARGETS tagcore_c RUNTIME DESTINATION "${HOST_WINDOWS_APPDIR}")
  elseif(NOT APPLE)
    install(TARGETS tagcore_c LIBRARY DESTINATION "${HOST_BUNDLE_INSTALL_DIR}/lib")
  endif()
endif()
