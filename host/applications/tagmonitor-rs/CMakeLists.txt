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
  shared library elsewhere (target tagcore_c), which host/CMakeLists.txt
  installs once for every Rust application.
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

include("${PROJECT_SOURCE_DIR}/cmake/HostRust.cmake")

#[[
  @brief  The cargo build, through host_rust_app() (cmake/HostRust.cmake),
          which links tagcore_c from this build: TAGCORE_C_LIB_DIR for the
          tagcore-c-sys build script, TAGCORE_C_RPATH on Linux, and the
          dependency on the library target.
#]]
host_rust_app(tagmonitor PACKAGE tagmonitor OUTPUT _tagmonitor_bin
  FEATURES sqlitelog LINKS_TAGCORE_C
  COMMENT "Building Tag Monitor (tagmonitor-rs) with cargo")

#[[
  @brief  Installs the program in each platform's package layout.

  @details macOS: the .app bundle is assembled from cargo's executable, a
           configured Info.plist and the icon, then signed at install time by
           macos_codesign_bundle, as for Tag Programmer. Windows: the program
           goes to HOST_WINDOWS_APPDIR (tag_tools/lib), beside tagcore_c.dll, and
           the GUI launcher to tag_tools under the program's name, since
           Windows finds a program's DLLs only beside it. Linux: tag_tools/bin;
           libtagcore_c.so is in tag_tools/lib, where its rpath points.
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
  install(
    PROGRAMS "$<TARGET_FILE:tag_tools_launcher_gui>"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME "tagmonitor${CMAKE_EXECUTABLE_SUFFIX}")
else()
  install(PROGRAMS "${_tagmonitor_bin}" DESTINATION "${HOST_BUNDLE_INSTALL_DIR}/bin")
endif()
