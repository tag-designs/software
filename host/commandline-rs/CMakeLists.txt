#[[
  The Rust command-line tools (the tag-tools crate), built with cargo and
  installed in the host tools package.

  From v4.0 these are the package's tag tools: tag-info, tag-start, tag-stop,
  tag-reset, tag-test, tag-cal and tag-dwnld replace the C++ tools of the same
  names, and tag-flash and base-flash program tags and bases without
  STM32CubeProgrammer. The crate's developer and bench tools are built into
  bin/internal and not installed. The cargo build is host_rust_tools()
  (cmake/HostRust.cmake): one build of the whole crate with the sqlitelog
  feature, so tag-dwnld writes its log through the C++ writer in tagcore_c.
  The embedded `-download`, `-erase` and `-dfu` targets use tag-flash and
  base-flash through rust_flash_tools() (cmake/RustFlashTools.cmake), which in
  this tree depends on the same build.

  Installed to tag_tools/bin on macOS and Linux. On Linux tag-dwnld finds
  libtagcore_c.so in tag_tools/lib through its run-time search path, and the
  0483:df11 line of linux/70-ultralight-tags.rules lets an ordinary user open a
  base in DFU mode. On Windows the ported tools are installed as the C++
  tools were: the program in tag_tools/lib beside tagcore_c.dll and the
  console launcher under its name in CMAKE_INSTALL_BINDIR; tag-flash and
  base-flash, which link no DLL of ours, go straight to CMAKE_INSTALL_BINDIR.
  They reach the base and the DFU bootloader through WinUSB, which Windows 11
  supplies.

  Added by host/CMakeLists.txt when BUILD_RUST_APPS is ON. Uses from the
  parent scope: HOST_BUNDLE_INSTALL_DIR, CMAKE_INSTALL_BINDIR,
  HOST_WINDOWS_APPDIR (Windows), and for signing MACOS_SIGN_APPS,
  MACOS_CODE_SIGN_IDENTITY and MACOS_CODE_SIGN_ENTITLEMENTS.
#]]
cmake_minimum_required(VERSION 3.20)

include("${PROJECT_SOURCE_DIR}/cmake/HostRust.cmake")
host_rust_tools()
include("${PROJECT_SOURCE_DIR}/cmake/RustFlashTools.cmake")
rust_flash_tools(_have_flash_tools)

#[[
  @brief  Installs the shipped tools in each platform's package layout, and
          signs them on macOS.

  @details The macOS signature is applied at install time by
           macos_codesign_executable() (cmake/MacosCodesignBundle.cmake), the
           code that signs the C++ tools, so that notarization of the image
           does not fail on cargo's ad-hoc signature. host/tools/sign-ci-macos.sh
           re-signs everything in tag_tools/bin of a CI-built DMG, these
           included.
#]]
if(WIN32)
  foreach(_tool IN LISTS HOST_RUST_SHIPPED_TOOLS)
    set(_exe "${HOST_RUST_TOOL_DIR}/${_tool}${CMAKE_EXECUTABLE_SUFFIX}")
    if(_tool STREQUAL "tag-flash" OR _tool STREQUAL "base-flash")
      install(PROGRAMS "${_exe}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    else()
      install(PROGRAMS "${_exe}" DESTINATION "${HOST_WINDOWS_APPDIR}")
      install(
        PROGRAMS "$<TARGET_FILE:tag_tools_launcher_console>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME "${_tool}${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
  endforeach()
else()
  set(_destination "${HOST_BUNDLE_INSTALL_DIR}/bin")
  foreach(_tool IN LISTS HOST_RUST_SHIPPED_TOOLS)
    install(PROGRAMS "${HOST_RUST_TOOL_DIR}/${_tool}" DESTINATION "${_destination}")
  endforeach()
  if(APPLE AND MACOS_SIGN_APPS AND MACOS_CODE_SIGN_IDENTITY)
    find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
    foreach(_tool IN LISTS HOST_RUST_SHIPPED_TOOLS)
      install(CODE "
        include(\"${PROJECT_SOURCE_DIR}/cmake/MacosCodesignBundle.cmake\")
        macos_codesign_executable(
            EXECUTABLE \"\${CMAKE_INSTALL_PREFIX}/${_destination}/${_tool}\"
            IDENTITY \"${MACOS_CODE_SIGN_IDENTITY}\"
            ENTITLEMENTS \"${MACOS_CODE_SIGN_ENTITLEMENTS}\"
            CODESIGN \"${CODESIGN_EXECUTABLE}\")
      ")
    endforeach()
  endif()
endif()
