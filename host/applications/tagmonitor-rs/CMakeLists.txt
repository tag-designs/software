#[[
  Tag Monitor (tagmonitor-rs): the Rust port of qtmonitor, built with Cargo and
  installed into the host tools package beside the Qt applications as
  tagmonitor, as Tag Programmer (tagprogram-rs) is.

  Cargo does the building; this file runs it and installs the result where
  each package expects an application:
    macOS    tag_tools/tagmonitor.app, a bundle assembled here (Info.plist,
             icon) and signed by the same code as the Qt bundles;
    Windows  tag_tools/tagmonitor.exe, the GUI launcher, which starts the
             real program from tag_tools/lib beside tagcore_c.dll and the VC
             runtime, as for the Qt applications;
    Linux    tag_tools/bin/tagmonitor, with libtagcore_c.so in tag_tools/lib.

  Download Data... writes SQLite through the C++ writer, so the build turns
  on the sqlitelog feature and links tagcore_c from this CMake build: the
  static archive inside the macOS executable (target tagcore_c_static), the
  shared library elsewhere (target tagcore_c), installed with the program.
  host/CMakeLists.txt builds tagcore_c whenever BUILD_RUST_APPS is ON.

  Needs cargo (Rust 1.85 or newer); Linux needs the WebKitGTK 4.1
  development package, as Tag Programmer does. See README.md and
  design/build-and-packaging.md.

  Added by host/CMakeLists.txt when BUILD_RUST_APPS is ON. Uses from the
  parent scope: HOST_BUNDLE_INSTALL_DIR, CMAKE_INSTALL_BINDIR and
  HOST_WINDOWS_APPDIR (Windows), CMAKE_OSX_DEPLOYMENT_TARGET,
  ULTRALIGHT_PACKAGE_VERSION, and for signing MACOS_SIGN_APPS,
  MACOS_CODE_SIGN_IDENTITY and MACOS_CODE_SIGN_ENTITLEMENTS.
#]]
cmake_minimum_required(VERSION 3.20)

#[[
  @brief  Finds cargo, also where rustup puts it but PATH does not reach.
#]]
find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
if(NOT CARGO_EXECUTABLE)
  message(FATAL_ERROR
    "BUILD_RUST_APPS is ON but cargo was not found. Install Rust (rustup.rs) "
    "or configure with -DBUILD_RUST_APPS=OFF.")
endif()

#[[
  @brief  The environment cargo runs in.

  @details CARGO_TARGET_DIR keeps cargo's output in this build tree, as for
           Tag Programmer. TAGCORE_C_LIB_DIR is where tagcore_c's targets
           leave the library, which taglogwriter's build script links.
           TAGCORE_C_RPATH (Linux) is where the installed program finds the
           shared library: tag_tools/lib, beside the package's other
           libraries. MACOSX_DEPLOYMENT_TARGET makes rustc target the same
           macOS as the Qt applications.
#]]
set(_cargo_target_dir "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(_tagmonitor_bin "${_cargo_target_dir}/release/tagmonitor${CMAKE_EXECUTABLE_SUFFIX}")
set(_cargo_env
    "CARGO_TARGET_DIR=${_cargo_target_dir}"
    "TAGCORE_C_LIB_DIR=${CMAKE_BINARY_DIR}/$<CONFIG>/lib")
if(APPLE)
  set(_tagcore_c_target tagcore_c_static)
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND _cargo_env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
else()
  set(_tagcore_c_target tagcore_c)
  if(NOT WIN32)
    list(APPEND _cargo_env "TAGCORE_C_RPATH=$ORIGIN/../lib")
  endif()
endif()

# Always run: Cargo decides what is out of date. --locked builds exactly the
# committed Cargo.lock.
add_custom_target(
  tagmonitor ALL
  COMMAND ${CMAKE_COMMAND} -E env ${_cargo_env}
          "${CARGO_EXECUTABLE}" build --release --locked -p tagmonitor --features sqlitelog
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  BYPRODUCTS "${_tagmonitor_bin}"
  COMMENT "Building Tag Monitor (tagmonitor-rs) with cargo"
  USES_TERMINAL
  VERBATIM)
add_dependencies(tagmonitor ${_tagcore_c_target})

#[[
  @brief  Installs the program, and on Linux and Windows the shared
          tagcore_c it links, in each platform's package layout.

  @details macOS: the .app bundle is assembled from cargo's executable, a
           configured Info.plist and the icon, then signed at install time by
           macos_codesign_bundle, as for Tag Programmer. Windows: the program
           goes to HOST_WINDOWS_APPDIR (tag_tools/lib) with tagcore_c.dll, and
           the GUI launcher to tag_tools under the program's name, since
           Windows finds a program's DLLs only beside it. Linux: tag_tools/bin,
           and libtagcore_c.so in tag_tools/lib, where its rpath points.
#]]
if(APPLE)
  set(_bundle "${HOST_BUNDLE_INSTALL_DIR}/tagmonitor.app")
  configure_file(cmake/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
  install(PROGRAMS "${_tagmonitor_bin}" DESTINATION "${_bundle}/Contents/MacOS")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" DESTINATION "${_bundle}/Contents")
  install(FILES app/icons/icon.icns DESTINATION "${_bundle}/Contents/Resources")
  if(MACOS_SIGN_APPS AND MACOS_CODE_SIGN_IDENTITY)
    find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
    install(CODE "
      include(\"${PROJECT_SOURCE_DIR}/cmake/MacosCodesignBundle.cmake\")
      macos_codesign_bundle(
          BUNDLE \"\${CMAKE_INSTALL_PREFIX}/${_bundle}\"
          IDENTITY \"${MACOS_CODE_SIGN_IDENTITY}\"
          ENTITLEMENTS \"${MACOS_CODE_SIGN_ENTITLEMENTS}\"
          CODESIGN \"${CODESIGN_EXECUTABLE}\")
    ")
  endif()
elseif(WIN32)
  install(PROGRAMS "${_tagmonitor_bin}" DESTINATION "${HOST_WINDOWS_APPDIR}")
  install(TARGETS tagcore_c RUNTIME DESTINATION "${HOST_WINDOWS_APPDIR}")
  install(
    PROGRAMS "$<TARGET_FILE:tag_tools_launcher_gui>"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME "tagmonitor${CMAKE_EXECUTABLE_SUFFIX}")
else()
  install(PROGRAMS "${_tagmonitor_bin}" DESTINATION "${HOST_BUNDLE_INSTALL_DIR}/bin")
  install(TARGETS tagcore_c LIBRARY DESTINATION "${HOST_BUNDLE_INSTALL_DIR}/lib")
endif()
