#[[
  tag-flash and base-flash: the Rust command-line tools that program tags and
  bases without STM32CubeProgrammer, built with cargo and installed beside
  the C++ command-line tools.

  The other tools in this crate are ports of C++ tools the package already
  ships, so only these two are built and installed. The cargo build itself is
  defined by rust_flash_tools() (cmake/RustFlashTools.cmake), which the
  embedded `-download`, `-erase` and `-dfu` targets share.

  Installed to tag_tools/bin on macOS and Linux, and beside the other tools in
  CMAKE_INSTALL_BINDIR on Windows, where they are self-contained and reach
  the base and the DFU bootloader through WinUSB, which Windows 11 supplies.
  On Linux the 0483:df11 line of linux/70-ultralight-tags.rules lets an
  ordinary user open a base in DFU mode.

  Added by host/CMakeLists.txt when BUILD_RUST_APPS is ON. Uses from the
  parent scope: HOST_BUNDLE_INSTALL_DIR, CMAKE_INSTALL_BINDIR, and for
  signing MACOS_SIGN_APPS, MACOS_CODE_SIGN_IDENTITY and
  MACOS_CODE_SIGN_ENTITLEMENTS.
#]]
cmake_minimum_required(VERSION 3.20)

include("${PROJECT_SOURCE_DIR}/cmake/RustFlashTools.cmake")
rust_flash_tools(_have_flash_tools)
if(NOT TARGET flash_tools)
  message(FATAL_ERROR
    "BUILD_RUST_APPS is ON but cargo was not found. Install Rust (rustup.rs) "
    "or configure with -DBUILD_RUST_APPS=OFF.")
endif()

# flash_tools is not in ALL, because the embedded targets build it only when
# a board is programmed; the package needs it built by a plain build.
add_custom_target(flash_tools_all ALL)
add_dependencies(flash_tools_all flash_tools)

#[[
  @brief  Installs both executables in each platform's package layout, and
          signs them on macOS.

  @details The macOS signature is applied at install time by
           macos_codesign_executable() (cmake/MacosCodesignBundle.cmake), the
           code that signs the C++ tools, so that notarization of the image
           does not fail on cargo's ad-hoc signature. host/tools/sign-ci-macos.sh
           re-signs everything in tag_tools/bin of a CI-built DMG, these
           included.
#]]
if(WIN32)
  set(_destination "${CMAKE_INSTALL_BINDIR}")
else()
  set(_destination "${HOST_BUNDLE_INSTALL_DIR}/bin")
endif()
install(PROGRAMS "${TAG_FLASH_EXECUTABLE}" "${BASE_FLASH_EXECUTABLE}"
        DESTINATION "${_destination}")
if(APPLE AND MACOS_SIGN_APPS AND MACOS_CODE_SIGN_IDENTITY)
  find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
  foreach(_tool "${TAG_FLASH_EXECUTABLE}" "${BASE_FLASH_EXECUTABLE}")
    get_filename_component(_name "${_tool}" NAME)
    install(CODE "
      include(\"${PROJECT_SOURCE_DIR}/cmake/MacosCodesignBundle.cmake\")
      macos_codesign_executable(
          EXECUTABLE \"\${CMAKE_INSTALL_PREFIX}/${_destination}/${_name}\"
          IDENTITY \"${MACOS_CODE_SIGN_IDENTITY}\"
          ENTITLEMENTS \"${MACOS_CODE_SIGN_ENTITLEMENTS}\"
          CODESIGN \"${CODESIGN_EXECUTABLE}\")
    ")
  endforeach()
endif()
