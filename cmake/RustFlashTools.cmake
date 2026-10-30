#[[
  @file   RustFlashTools.cmake
  @brief  Builds tag-flash and base-flash, the Rust programming tools, with
          cargo, once per build tree.

  @details tag-flash programs, erases and reads a tag through a base, and
           base-flash programs a base through its ROM bootloader over USB DFU;
           together they replace STM32_Programmer_CLI (see
           host/commandline-rs/design/proposals/programming-without-cubeprogrammer.md).
           Two parts of the build need them: the host package installs them
           (host/commandline-rs/CMakeLists.txt) and the embedded `-download`,
           `-erase` and `-dfu` targets run them (embedded/CMakeLists.txt).
           Both call rust_flash_tools() here, so a tree that configures both
           compiles them once.

           Where cargo is not installed, rust_flash_tools() looks for
           tag-flash and base-flash on PATH instead, so a bench machine with
           the host tools package installed can still use them.

  @see    host/commandline-rs/README.md
#]]

#[[
  @brief  Defines the flash_tools target and reports where the executables
          will be.

  @details On the first call, with cargo found, adds the custom target
           `flash_tools`, which runs
           `cargo build --release --locked --bin tag-flash --bin base-flash`
           in host/commandline-rs with its target directory in this build
           tree, and sets the INTERNAL cache variables TAG_FLASH_EXECUTABLE
           and BASE_FLASH_EXECUTABLE to the paths it produces. The target is
           not part of ALL: a caller that needs the executables built depends
           on it. Without cargo, the two variables are what find_program()
           finds on PATH, and no target is defined. Later calls only return
           what the first one found.

  @param  out_found  Set in the caller's scope to TRUE when both executables
                     are available (built here or found), FALSE otherwise.

  Example:
    rust_flash_tools(_have_flash_tools)
    if(_have_flash_tools AND TARGET flash_tools)
      add_dependencies(my_target flash_tools)
    endif()
#]]
function(rust_flash_tools out_found)
  get_property(_done GLOBAL PROPERTY _RUST_FLASH_TOOLS_DONE)
  if(NOT _done)
    find_program(FLASH_TOOLS_CARGO cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
    if(FLASH_TOOLS_CARGO)
      set(_dir "${CMAKE_BINARY_DIR}/flash-tools")
      set(_tag_flash "${_dir}/release/tag-flash${CMAKE_EXECUTABLE_SUFFIX}")
      set(_base_flash "${_dir}/release/base-flash${CMAKE_EXECUTABLE_SUFFIX}")
      set(_env "CARGO_TARGET_DIR=${_dir}")
      if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND _env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
      endif()
      # Always run: cargo decides what is out of date. --locked builds
      # exactly the committed Cargo.lock. Both are then copied into bin/
      # beside the C++ tools.
      add_custom_target(
        flash_tools
        COMMAND ${CMAKE_COMMAND} -E env ${_env}
                "${FLASH_TOOLS_CARGO}" build --release --locked
                --bin tag-flash --bin base-flash
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_tag_flash}" "${_base_flash}"
                "${CMAKE_BINARY_DIR}/$<CONFIG>/bin"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/host/commandline-rs"
        BYPRODUCTS "${_tag_flash}" "${_base_flash}"
        COMMENT "Building tag-flash and base-flash with cargo"
        USES_TERMINAL
        VERBATIM)
      set(TAG_FLASH_EXECUTABLE "${_tag_flash}" CACHE INTERNAL "tag-flash, built by flash_tools")
      set(BASE_FLASH_EXECUTABLE "${_base_flash}" CACHE INTERNAL "base-flash, built by flash_tools")
    else()
      find_program(_found_tag_flash tag-flash)
      find_program(_found_base_flash base-flash)
      set(TAG_FLASH_EXECUTABLE "${_found_tag_flash}" CACHE INTERNAL "tag-flash, found on PATH")
      set(BASE_FLASH_EXECUTABLE "${_found_base_flash}" CACHE INTERNAL "base-flash, found on PATH")
    endif()
    # A plain variable in the top directory's scope would not reach a later
    # sibling directory; a global property does.
    set_property(GLOBAL PROPERTY _RUST_FLASH_TOOLS_DONE TRUE)
  endif()
  if(TAG_FLASH_EXECUTABLE AND BASE_FLASH_EXECUTABLE)
    set(${out_found} TRUE PARENT_SCOPE)
  else()
    set(${out_found} FALSE PARENT_SCOPE)
  endif()
endfunction()
