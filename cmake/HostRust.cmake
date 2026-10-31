#[[
  @file   HostRust.cmake
  @brief  The shared pieces of every CMake target that builds a host Rust
          crate with cargo: where cargo is, the one target directory, and
          host_rust_app(), which defines an application's build target.

  @details All of the host's Rust code is one Cargo workspace, host/Cargo.toml,
           with one Cargo.lock. Every cargo invocation CMake makes therefore
           runs in that workspace and shares one CARGO_TARGET_DIR,
           HOST_CARGO_TARGET_DIR (<build>/host/cargo), so the crates the
           applications share (tagcore, tagprobe, Tauri and the rest) are
           compiled once per build tree rather than once per application.
           cargo serialises concurrent invocations on that directory itself.

           Included by the Rust applications' CMakeLists.txt files
           (host/applications/*-rs) and by RustFlashTools.cmake, which builds
           tag-flash and base-flash from the same workspace. It defines no
           targets by itself and needs nothing from the host build, so an
           embedded-only tree may include it too.

  @see    host/README.md, "Rust crates"; host/design/guide/13-building-and-testing.md
#]]

#[[
  @brief  Finds cargo, also where rustup puts it but PATH does not reach
          (an IDE or CI shell that has not sourced ~/.cargo/env).
#]]
find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")

#[[
  @brief  The Cargo workspace every host Rust crate belongs to.
#]]
set(HOST_RUST_WORKSPACE_DIR "${PROJECT_SOURCE_DIR}/host")

#[[
  @brief  cargo's output directory for this build tree.

  @details Kept in the build tree rather than the source tree's host/target,
           so a CMake build writes nothing into the source directory and two
           build trees do not share (and fight over) one target directory.
           Release executables are at <HOST_CARGO_TARGET_DIR>/release/<name>.
#]]
set(HOST_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/host/cargo")

#[[
  @brief  Stops the configure with how to fix it when cargo is missing.

  @details Called by the application targets, which cannot fall back to
           anything; RustFlashTools.cmake instead looks for installed tools.
#]]
function(host_rust_require_cargo)
  if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR
      "BUILD_RUST_APPS is ON but cargo was not found. Install Rust (rustup.rs) "
      "or configure with -DBUILD_RUST_APPS=OFF.")
  endif()
endfunction()

#[[
  @brief  Defines the target that builds one Rust application with cargo.

  @details Adds a custom target, in ALL, that runs
           `cargo build --release --locked -p <PACKAGE> [--features ...]`
           in the host workspace with HOST_CARGO_TARGET_DIR, then copies the
           executable into <build>/<Config>/bin beside the C++ programs.
           cargo decides what is out of date, so the target always runs;
           --locked builds exactly the committed host/Cargo.lock.

           The environment always carries CARGO_TARGET_DIR and, on macOS,
           MACOSX_DEPLOYMENT_TARGET, so rustc and the C code crates compile
           (libusb) target the same macOS as the Qt applications. With
           LINKS_TAGCORE_C it also carries what the tagcore-c-sys build
           script needs to link the C++ host libraries: TAGCORE_C_LIB_DIR
           (<build>/<Config>/lib, where the tagcore_c targets put the
           library) and, on Linux, TAGCORE_C_RPATH=$ORIGIN/../lib, the
           installed program's run-time search path; and the target depends
           on tagcore_c_static (macOS, linked into the executable) or
           tagcore_c (elsewhere, a shared library the package installs once).

  @param  target          The CMake target name, e.g. tagmonitor.
  @param  PACKAGE         The cargo package, which is also the executable's
                          name, e.g. tagmonitor.
  @param  OUTPUT          A variable set in the caller's scope to the
                          executable's path, for the install rules.
  @param  FEATURES        Cargo features to enable (optional).
  @param  ENV             Further NAME=VALUE pairs for cargo's environment
                          (optional).
  @param  LINKS_TAGCORE_C The crate links the tagcore_c library (see above).
  @param  COMMENT         The build step's message.

  Example:
    host_rust_app(tagmonitor PACKAGE tagmonitor OUTPUT _tagmonitor_bin
      FEATURES sqlitelog LINKS_TAGCORE_C
      COMMENT "Building Tag Monitor (tagmonitor-rs) with cargo")
    install(PROGRAMS "${_tagmonitor_bin}" DESTINATION ...)
#]]
function(host_rust_app target)
  cmake_parse_arguments(ARG "LINKS_TAGCORE_C" "PACKAGE;OUTPUT;COMMENT" "FEATURES;ENV" ${ARGN})
  if(NOT ARG_PACKAGE OR NOT ARG_OUTPUT)
    message(FATAL_ERROR "host_rust_app(${target}): PACKAGE and OUTPUT are required")
  endif()
  host_rust_require_cargo()

  set(_bin "${HOST_CARGO_TARGET_DIR}/release/${ARG_PACKAGE}${CMAKE_EXECUTABLE_SUFFIX}")
  set(_env "CARGO_TARGET_DIR=${HOST_CARGO_TARGET_DIR}" ${ARG_ENV})
  if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND _env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()

  set(_depends)
  if(ARG_LINKS_TAGCORE_C)
    list(APPEND _env "TAGCORE_C_LIB_DIR=${CMAKE_BINARY_DIR}/$<CONFIG>/lib")
    if(APPLE)
      set(_depends tagcore_c_static)
    else()
      set(_depends tagcore_c)
      if(NOT WIN32)
        list(APPEND _env "TAGCORE_C_RPATH=$ORIGIN/../lib")
      endif()
    endif()
  endif()

  set(_features)
  if(ARG_FEATURES)
    string(JOIN "," _feature_list ${ARG_FEATURES})
    set(_features --features "${_feature_list}")
  endif()

  # BYPRODUCTS names the executable so the install rules, and generators such
  # as Ninja, know which step produces it.
  add_custom_target(
    ${target} ALL
    COMMAND ${CMAKE_COMMAND} -E env ${_env}
            "${CARGO_EXECUTABLE}" build --release --locked -p ${ARG_PACKAGE} ${_features}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_bin}" "${CMAKE_BINARY_DIR}/$<CONFIG>/bin"
    WORKING_DIRECTORY "${HOST_RUST_WORKSPACE_DIR}"
    BYPRODUCTS "${_bin}"
    COMMENT "${ARG_COMMENT}"
    USES_TERMINAL
    VERBATIM)
  if(_depends)
    add_dependencies(${target} ${_depends})
  endif()
  set(${ARG_OUTPUT} "${_bin}" PARENT_SCOPE)
endfunction()
