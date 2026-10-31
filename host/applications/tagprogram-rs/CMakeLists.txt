#[[
  Tag Programmer (tagprogram-rs): the Rust port of qtprogram, built with Cargo
  and installed into the host tools package beside the Qt applications.

  Cargo does the building; this file runs it and installs the result where
  each package expects an application:
    macOS    tag_tools/tagprogram.app, a bundle assembled here (Info.plist,
             icon) and signed by the same code as the Qt bundles;
    Windows  tag_tools/tagprogram.exe, self-contained (it needs the WebView2
             runtime that Windows 10 and 11 carry);
    Linux    tag_tools/bin/tagprogram, which uses the system's WebKitGTK 4.1.

  Needs cargo (Rust 1.85 or newer). libusb and protoc are vendored by the
  crates, so nothing else is required on macOS and Windows; Linux needs the
  WebKitGTK 4.1 development package. The record's host_version is set to the
  package version, as qtprogram's is. See README.md and
  design/build-and-packaging.md.

  Added by host/CMakeLists.txt when BUILD_RUST_APPS is ON. Uses from the
  parent scope: ULTRALIGHT_PACKAGE_VERSION, HOST_BUNDLE_INSTALL_DIR,
  CMAKE_INSTALL_BINDIR (tag_tools on Windows), CMAKE_OSX_DEPLOYMENT_TARGET,
  and for signing MACOS_SIGN_APPS, MACOS_CODE_SIGN_IDENTITY and
  MACOS_CODE_SIGN_ENTITLEMENTS.
#]]
cmake_minimum_required(VERSION 3.20)

include("${PROJECT_SOURCE_DIR}/cmake/HostRust.cmake")

#[[
  @brief  The cargo build, through host_rust_app() (cmake/HostRust.cmake).

  @details TAGPROGRAM_HOST_VERSION is read at compile time by the engine
           (option_env!) for the record's host_version column; without it
           the crate's own version is used.
#]]
host_rust_app(tagprogram PACKAGE tagprogram OUTPUT _tagprogram_bin
  ENV "TAGPROGRAM_HOST_VERSION=${ULTRALIGHT_PACKAGE_VERSION}"
  COMMENT "Building Tag Programmer (tagprogram-rs) with cargo")

#[[
  @brief  Installs the executable in each platform's package layout.

  @details macOS: cargo builds a bare executable, so the .app bundle is
           assembled here from it, a configured Info.plist and the icon.
           Info.plist carries NSDocumentsFolderUsageDescription (and the
           Desktop and Downloads equivalents) because macOS privacy control
           (TCC) otherwise refuses the first open in ~/Documents -- where the
           record lives -- for an app signed with an ad-hoc identity. When
           signing is enabled the bundle is signed at install time by
           macos_codesign_bundle (cmake/MacosCodesignBundle.cmake), the same
           code that signs the Qt bundles; it must run after every file is in
           the bundle, hence install(CODE).
           Windows: the .exe goes in CMAKE_INSTALL_BINDIR beside the Qt tools.
           Linux: tag_tools/bin; the system must provide WebKitGTK 4.1.
#]]
if(APPLE)
  set(_bundle "${HOST_BUNDLE_INSTALL_DIR}/tagprogram.app")
  configure_file(cmake/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
  install(PROGRAMS "${_tagprogram_bin}" DESTINATION "${_bundle}/Contents/MacOS")
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
  install(PROGRAMS "${_tagprogram_bin}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
  install(PROGRAMS "${_tagprogram_bin}" DESTINATION "${HOST_BUNDLE_INSTALL_DIR}/bin")
endif()
