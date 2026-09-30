cmake_minimum_required(VERSION 3.20)

#
# Create a tool for translating json tag configuration files to 
# C code
#

# config-gen translates default-config.json into the proto3 wire encoding.  Its
# output is committed like the nanopb output, so it is only needed when sources
# are regenerated; REGENERATE_SOURCES=OFF is the mode that builds firmware
# without it.

set(CONFIG_GEN_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/config-gen.py")
set(CONFIG_GEN_DESCRIPTOR "${CMAKE_CURRENT_BINARY_DIR}/config-gen/tag.desc")

if(NOT REGENERATE_SOURCES STREQUAL "OFF")
  # config-gen is a Python script rather than a compiled C++ tool.  It emits
  # byte-identical output -- verified against the committed default_config.c of
  # every distributed variant, and against a rebuild of the old config-gen.cc.
  #
  # Be clear about what this costs, because it is easy to state too kindly.
  # nanopb's BINARY distribution ships its own interpreter under generator-bin,
  # with protobuf bundled, so regeneration did NOT already require protobuf on
  # the interpreter CMake finds here.  On a machine using that distribution
  # this is a genuinely new dependency on the system Python, not a free ride on
  # an existing one.  What is gained in exchange:
  #
  #   * The dependency becomes pinnable.  libprotobuf came from each
  #     developer's vcpkg baseline, so its version was per-machine by
  #     construction and appears in no manifest.  A Python version can be
  #     recorded and hashed; a vcpkg baseline's build of a C++ library cannot.
  #   * One protobuf implementation governs default_config.c instead of a
  #     second one living beside nanopb's in the same generated/ directory.
  #   * It removes a host compile-and-link from a cross-compiling tree.
  #
  # The honest follow-up is a virtual environment built from a hash-pinned
  # requirements.txt, created at configure time, rather than asking developers
  # to pip install into whatever interpreter they have.  That is what makes the
  # version recordable in the manifest; until it exists, the protobuf version
  # is deliberately absent from tools_text for the same reason fmpp is absent
  # from the board manifests -- a version reported on one machine and not
  # another causes false staleness for everyone.
  #
  # The JSON parsers are where the two implementations genuinely differed: the
  # C++ one accepted trailing commas, which RFC 8259 forbids, and two config
  # files relied on that.  Both were corrected.  Strictness here is the point,
  # not a regression.
  # Which interpreter runs config-gen.py is not a detail to leave to PATH.
  # On a developer machine `python3`, `pip` and CMake's find_package(Python3)
  # routinely resolve to three different installations -- Homebrew ships
  # several side by side, and pip commonly belongs to none of the ones in use.
  # Leaving the choice open means the version of protobuf that renders a
  # COMMITTED file varies per machine and per shell, which is the exact defect
  # the generated sources are pinned to avoid.
  #
  # So the build makes its own environment from requirements.txt and uses that,
  # rather than asking developers to install packages into an interpreter they
  # share with everything else. Nothing here touches the system Python.
  #
  # This costs a virtualenv and one pip run in the build tree, on the first
  # configure and afterwards only when requirements.txt changes. It needs
  # network access that once. REGENERATE_SOURCES=OFF never reaches this block,
  # so ordinary firmware builds and CI release builds are untouched and stay
  # offline-capable.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  option(CONFIG_GEN_USE_VENV
         "Build a virtual environment for config-gen.py from requirements.txt"
         ON)
  set(CONFIG_GEN_REQUIREMENTS "${CMAKE_CURRENT_LIST_DIR}/requirements.txt")

  if(CONFIG_GEN_USE_VENV)
    set(CONFIG_GEN_VENV "${CMAKE_CURRENT_BINARY_DIR}/config-gen-venv")
    if(WIN32)
      set(CONFIG_GEN_PYTHON "${CONFIG_GEN_VENV}/Scripts/python.exe")
    else()
      set(CONFIG_GEN_PYTHON "${CONFIG_GEN_VENV}/bin/python")
    endif()

    # The stamp carries the hash of requirements.txt, so an edit to it rebuilds
    # the environment and nothing else does. Timestamps are not used, for the
    # same reason the generated-source manifests do not use them.
    file(SHA256 "${CONFIG_GEN_REQUIREMENTS}" _config_gen_req_hash)
    set(_config_gen_stamp "${CONFIG_GEN_VENV}/.requirements.sha256")
    set(_config_gen_stamped "")
    if(EXISTS "${_config_gen_stamp}")
      file(READ "${_config_gen_stamp}" _config_gen_stamped)
    endif()

    if(NOT EXISTS "${CONFIG_GEN_PYTHON}"
       OR NOT _config_gen_stamped STREQUAL _config_gen_req_hash)
      message(STATUS "config-gen: building ${CONFIG_GEN_VENV}")
      file(REMOVE_RECURSE "${CONFIG_GEN_VENV}")
      execute_process(
        COMMAND "${Python3_EXECUTABLE}" -m venv "${CONFIG_GEN_VENV}"
        RESULT_VARIABLE _config_gen_venv_made
        ERROR_VARIABLE _config_gen_venv_err)
      if(NOT _config_gen_venv_made EQUAL 0)
        message(FATAL_ERROR
          "Could not create a virtual environment with ${Python3_EXECUTABLE}:\n"
          "${_config_gen_venv_err}\n"
          "Install the venv module for that interpreter, or configure with "
          "-DCONFIG_GEN_USE_VENV=OFF and -DPython3_EXECUTABLE=<an interpreter "
          "that already has the packages in ${CONFIG_GEN_REQUIREMENTS}>.")
      endif()
      execute_process(
        COMMAND "${CONFIG_GEN_PYTHON}" -m pip install --quiet --disable-pip-version-check
                -r "${CONFIG_GEN_REQUIREMENTS}"
        RESULT_VARIABLE _config_gen_pip
        ERROR_VARIABLE _config_gen_pip_err)
      if(NOT _config_gen_pip EQUAL 0)
        message(FATAL_ERROR
          "Could not install ${CONFIG_GEN_REQUIREMENTS} into "
          "${CONFIG_GEN_VENV}:\n${_config_gen_pip_err}\n"
          "This step needs network access the first time. To use an "
          "interpreter you have prepared yourself instead, configure with "
          "-DCONFIG_GEN_USE_VENV=OFF -DPython3_EXECUTABLE=<that interpreter>.")
      endif()
      file(WRITE "${_config_gen_stamp}" "${_config_gen_req_hash}")
    endif()
  else()
    # Opt-out: whatever find_package found, or whatever was pinned on the
    # command line. The probes below still apply, so a wrong interpreter fails
    # here with a message rather than in the middle of a build.
    set(CONFIG_GEN_PYTHON "${Python3_EXECUTABLE}")
  endif()

  execute_process(
    COMMAND "${CONFIG_GEN_PYTHON}" -c
            "import google.protobuf as p; print(p.__version__)"
    OUTPUT_VARIABLE CONFIG_GEN_PROTOBUF_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _config_gen_protobuf_probe)
  if(NOT _config_gen_protobuf_probe EQUAL 0)
    message(FATAL_ERROR
      "${CONFIG_GEN_PYTHON} cannot import google.protobuf, which config-gen.py "
      "needs to turn default-config.json into the proto3 wire encoding. Install "
      "the protobuf package for that interpreter, point Python3_EXECUTABLE at "
      "the one that already runs nanopb_generator, or configure with "
      "-DREGENERATE_SOURCES=OFF to build the committed sources as they are.")
  endif()
  message(STATUS
    "config-gen: ${CONFIG_GEN_PYTHON} with protobuf ${CONFIG_GEN_PROTOBUF_VERSION}")

  # protoc renders the descriptor set config-gen.py reads its schema from.
  # It is NOT a Python dependency: the pinned nanopb distribution ships one in
  # generator-bin beside nanopb_generator, so a protoc is present exactly when
  # regeneration is possible. Preferring that one also means the descriptor set
  # and the .pb.c/.pb.h beside it come from the same protoc.
  #
  # Taking protoc from PyPI instead would mean grpcio-tools, which pulls grpcio
  # -- some 12 MB -- for a binary already on disk.
  #
  # Version sensitivity here is low by construction. A descriptor set is
  # wire-format data, not generated code, so it carries no gencode version for
  # the runtime to reject; any protoc that emits one produces something any
  # modern protobuf can read.
  # protoc is normally the one the nanopb distribution ships beside its
  # generator, so that the descriptor set and the .pb.c files come from the
  # same release.  That tie has to survive nanopb MOVING: a plain cache
  # variable would be derived once and then keep naming a protoc from a
  # distribution no longer in use, and the version check below would compare
  # against a binary nothing runs.  So the nanopb generator it was derived from
  # is remembered, and the search is redone when that changes.
  #
  # A protoc the user set explicitly is never overwritten: re-derivation
  # happens only while the cached value is still the one this logic chose.
  set(CONFIG_GEN_PROTOC "" CACHE FILEPATH
      "protoc used to render the config-gen descriptor set")
  # Initialised only when absent: `set(... CACHE INTERNAL ...)` implies FORCE,
  # so writing these unconditionally would erase them on every configure --
  # which is precisely the memory the re-derivation below depends on.
  if(NOT DEFINED CONFIG_GEN_PROTOC_DERIVED_FROM)
    set(CONFIG_GEN_PROTOC_DERIVED_FROM "" CACHE INTERNAL
        "nanopb generator CONFIG_GEN_PROTOC was derived from")
  endif()
  if(NOT DEFINED CONFIG_GEN_PROTOC_DERIVED)
    set(CONFIG_GEN_PROTOC_DERIVED "" CACHE INTERNAL
        "the protoc this logic last derived")
  endif()

  set(_config_gen_rederive FALSE)
  if(NOT CONFIG_GEN_PROTOC)
    set(_config_gen_rederive TRUE)
  elseif(CONFIG_GEN_PROTOC STREQUAL CONFIG_GEN_PROTOC_DERIVED)
    # Ours, not the user's. Redo it if nanopb moved or the binary is gone.
    if(NOT CONFIG_GEN_PROTOC_DERIVED_FROM STREQUAL "${NANOPB_GENERATOR_EXECUTABLE}"
       OR NOT EXISTS "${CONFIG_GEN_PROTOC}")
      set(_config_gen_rederive TRUE)
    endif()
  endif()

  if(_config_gen_rederive)
    set(_config_gen_protoc_hints "")
    if(NANOPB_GENERATOR_EXECUTABLE)
      get_filename_component(_config_gen_nanopb_bin
                             "${NANOPB_GENERATOR_EXECUTABLE}" DIRECTORY)
      list(APPEND _config_gen_protoc_hints "${_config_gen_nanopb_bin}")
    endif()
    # find_program caches into its own result variable, so a stale one would
    # defeat the re-derivation above.
    unset(_config_gen_protoc_found CACHE)
    find_program(_config_gen_protoc_found
                 NAMES protoc
                 HINTS ${_config_gen_protoc_hints}
                 NO_DEFAULT_PATH)
    if(NOT _config_gen_protoc_found AND Protobuf_PROTOC_EXECUTABLE)
      set(_config_gen_protoc_found "${Protobuf_PROTOC_EXECUTABLE}")
    endif()
    if(NOT _config_gen_protoc_found)
      unset(_config_gen_protoc_found CACHE)
      find_program(_config_gen_protoc_found NAMES protoc)
    endif()
    if(NOT _config_gen_protoc_found)
      message(FATAL_ERROR
        "No protoc found to render the descriptor set config-gen.py needs. "
        "It normally comes from the nanopb distribution's generator-bin "
        "directory, beside nanopb_generator. Set NANOPB_ROOT, set "
        "CONFIG_GEN_PROTOC to a protoc, put one on PATH, or configure with "
        "-DREGENERATE_SOURCES=OFF to build the committed sources as they are.")
    endif()
    set(CONFIG_GEN_PROTOC "${_config_gen_protoc_found}" CACHE FILEPATH
        "protoc used to render the config-gen descriptor set" FORCE)
    set(CONFIG_GEN_PROTOC_DERIVED "${_config_gen_protoc_found}" CACHE INTERNAL
        "the protoc this logic last derived")
    set(CONFIG_GEN_PROTOC_DERIVED_FROM "${NANOPB_GENERATOR_EXECUTABLE}"
        CACHE INTERNAL "nanopb generator CONFIG_GEN_PROTOC was derived from")
  endif()
  set(CONFIG_GEN_PROTOC_COMMAND "${CONFIG_GEN_PROTOC}")
  set(CONFIG_GEN_PROTOC_LABEL "${CONFIG_GEN_PROTOC}")
  message(STATUS "config-gen protoc: ${CONFIG_GEN_PROTOC}")

  get_target_property(_config_gen_protos tag-proto-sources INTERFACE_SOURCES)
  if(NOT _config_gen_protos)
    message(FATAL_ERROR
      "tag-proto-sources carries no INTERFACE_SOURCES, so there is nothing to "
      "generate Python bindings from.")
  endif()

  # The include path is taken from the .proto files themselves rather than
  # spelled relative to here. protoc requires -I to be an EXACT TEXTUAL PREFIX
  # of each file it is given -- it does not resolve `..` or compare absolute
  # against relative -- so "${CMAKE_CURRENT_LIST_DIR}/../../proto" fails
  # against the absolute paths the target property holds, even though the two
  # name the same directory. Deriving the directory from a file that is
  # certainly inside it removes the chance of the two disagreeing.
  list(GET _config_gen_protos 0 _config_gen_first_proto)
  cmake_path(GET _config_gen_first_proto PARENT_PATH CONFIG_GEN_PROTO_DIR)

  # The pinned protobuf and the protoc in use must be the same release.
  #
  # requirements.txt names a version; nothing derives it from nanopb, and
  # nothing could reliably -- a source nanopb tree ships no protoc at all. So
  # a nanopb bump would move protoc while the pin sat still, quietly undoing
  # the property the pin exists for: that ONE protobuf release governs both
  # generated outputs in these directories.
  #
  # Rather than derive it, check it. protoc reports "libprotoc 28.1"; the
  # Python package calls the same release 5.28.1. The Python-side major differs
  # by design, so the comparison is on what follows it.
  #
  # Releases before 22 complicate this: protoc called them 3.21.12 while the
  # Python package called the same release 4.21.12. So a leading "3." on the
  # protoc side is dropped as well, leaving 21.12 against 21.12.
  execute_process(
    COMMAND "${CONFIG_GEN_PROTOC}" --version
    OUTPUT_VARIABLE _config_gen_protoc_version_text
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _config_gen_protoc_version_ok
    ERROR_QUIET)
  if(NOT _config_gen_protoc_version_ok EQUAL 0)
    message(WARNING
      "Could not run ${CONFIG_GEN_PROTOC} --version; the protoc and protobuf "
      "versions are unverified against each other.")
  else()
    string(REGEX MATCH "([0-9]+\\.[0-9]+(\\.[0-9]+)?)$"
           _ "${_config_gen_protoc_version_text}")
    set(_config_gen_protoc_version "${CMAKE_MATCH_1}")
    # protobuf 5.28.1 -> 28.1, protobuf 4.21.12 -> 21.12
    string(REGEX REPLACE "^[0-9]+\\.(.*)$" "\\1" _config_gen_protobuf_tail
           "${CONFIG_GEN_PROTOBUF_VERSION}")
    # protoc 28.1 -> 28.1, protoc 3.21.12 -> 21.12
    string(REGEX REPLACE "^3\\.(.*)$" "\\1" _config_gen_protoc_tail
           "${_config_gen_protoc_version}")
    if(NOT _config_gen_protoc_tail STREQUAL _config_gen_protobuf_tail)
      reproducibility_problem(
        "config-gen's protobuf and protoc are different releases: "
        "requirements.txt pins protobuf ${CONFIG_GEN_PROTOBUF_VERSION} but "
        "${CONFIG_GEN_PROTOC} reports ${_config_gen_protoc_version_text}. "
        "protoc normally comes from the nanopb distribution, so this usually "
        "means nanopb was bumped without updating "
        "${CONFIG_GEN_REQUIREMENTS}. Set protobuf to "
        "<major>.${_config_gen_protoc_tail} there, regenerate, and commit "
        "what changes.")
    else()
      message(STATUS
        "config-gen: protobuf ${CONFIG_GEN_PROTOBUF_VERSION} and "
        "${_config_gen_protoc_version_text} are the same release")
    endif()
  endif()

  # Prove this works now rather than in the middle of a build: render a
  # descriptor set with this protoc and build the Config class from it with
  # this interpreter. A failure here names the cause; the same failure during
  # a build arrives with five targets in flight.
  set(_config_gen_probe "${CMAKE_CURRENT_BINARY_DIR}/config-gen-probe")
  file(MAKE_DIRECTORY "${_config_gen_probe}")
  execute_process(
    COMMAND "${CONFIG_GEN_PROTOC}"
            "-I${CONFIG_GEN_PROTO_DIR}" --include_imports
            "--descriptor_set_out=${_config_gen_probe}/tag.desc"
            ${_config_gen_protos}
    RESULT_VARIABLE _config_gen_probe_gen
    OUTPUT_QUIET ERROR_VARIABLE _config_gen_probe_err)
  if(NOT _config_gen_probe_gen EQUAL 0)
    message(FATAL_ERROR
      "${CONFIG_GEN_PROTOC} could not render a descriptor set for the tag "
      "protocol:\n${_config_gen_probe_err}")
  endif()
  execute_process(
    COMMAND "${CONFIG_GEN_PYTHON}" "${CONFIG_GEN_SCRIPT}"
            --descriptor-set "${_config_gen_probe}/tag.desc"
            --print-config-only
    RESULT_VARIABLE _config_gen_probe_load
    OUTPUT_QUIET ERROR_VARIABLE _config_gen_probe_load_err)
  if(NOT _config_gen_probe_load EQUAL 0)
    message(FATAL_ERROR
      "config-gen.py could not build the Config message from a descriptor set "
      "rendered by ${CONFIG_GEN_PROTOC}, using protobuf "
      "${CONFIG_GEN_PROTOBUF_VERSION}:\n${_config_gen_probe_load_err}")
  endif()

  add_custom_command(
    OUTPUT "${CONFIG_GEN_DESCRIPTOR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/config-gen"
    COMMAND "${CONFIG_GEN_PROTOC}"
            "-I${CONFIG_GEN_PROTO_DIR}" --include_imports
            "--descriptor_set_out=${CONFIG_GEN_DESCRIPTOR}"
            ${_config_gen_protos}
    DEPENDS ${_config_gen_protos}
    COMMENT "Rendering the protocol descriptor set for config-gen"
    VERBATIM)

  add_custom_target(config-gen-descriptor DEPENDS "${CONFIG_GEN_DESCRIPTOR}")
endif()

# Captured at file scope: inside a function CMAKE_CURRENT_LIST_DIR is the
# CALLER's directory, not this file's, and add_nanopb_target is called from each
# variant's subdirectory.
include("${CMAKE_SOURCE_DIR}/cmake/GeneratedSourceInputs.cmake")

set(NANOPB_PROTO_C_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(NANOPB_DEFAULT_OPTIONS_DIR "${NANOPB_PROTO_C_DIR}/default-options")


#
# Helper function to create nanopb pb.h pb.c files
#
function(add_nanopb_target name)

  get_target_property(TAG_PROTO_SOURCES tag-proto-sources INTERFACE_SOURCES)

  set_property(GLOBAL APPEND PROPERTY ULTRALIGHT_PROTO_ALL "${name}_proto")

  # Generated sources are committed here so that an ordinary firmware build
  # needs neither the nanopb generator nor config-gen.  Regeneration writes back
  # into this directory rather than the build tree, so what is built is what is
  # committed.

  # A variant commits its generated sources by having a generated/ directory.
  # Only the distributed variants do; prototypes generate into the build tree as
  # before, so an ordinary build never leaves untracked files in the source tree
  # for tags that are not shipped.  To promote a prototype, create the directory
  # and configure with -DREGENERATE_SOURCES=ON once, then commit what lands in
  # it -- the consistency check in embedded/CMakeLists.txt reports a distributed
  # variant that has not had this done.
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated")
    set(generated_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated")
    set(sources_committed TRUE)
    set_property(GLOBAL APPEND PROPERTY ULTRALIGHT_PROTO_COMMITTED "${name}_proto")
  else()
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}")
    set(sources_committed FALSE)
  endif()
  set(manifest "${generated_dir}/inputs.sha256")
  set(default_config "${generated_dir}/default_config.c")
  set(default_config_json "${CMAKE_CURRENT_SOURCE_DIR}/default-config.json")

  set(nanopb_sources)
  set(nanopb_headers)
  set(nanopb_options)

  # The manifest input list is shared with CheckGeneratedSourcesFresh.cmake so
  # that the build and the check cannot disagree about it. They once did.
  proto_c_manifest_inputs(manifest_inputs
                          SOURCE_DIR "${CMAKE_SOURCE_DIR}"
                          VARIANT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
                          PROTO_SOURCES ${TAG_PROTO_SOURCES})

  foreach(P ${TAG_PROTO_SOURCES})
    cmake_path(GET P STEM stem)
    list(APPEND nanopb_sources "${generated_dir}/${stem}.pb.c")
    list(APPEND nanopb_headers "${generated_dir}/${stem}.pb.h")
    list(APPEND nanopb_options "${CMAKE_CURRENT_BINARY_DIR}/${stem}.options")

    set(default_options "${NANOPB_DEFAULT_OPTIONS_DIR}/${stem}.options")
    set(override_options "${CMAKE_CURRENT_SOURCE_DIR}/${stem}.override.options")
    if(NOT EXISTS "${default_options}")
      message(FATAL_ERROR "Missing default nanopb options file: ${default_options}")
    endif()
    if(NOT EXISTS "${override_options}")
      message(FATAL_ERROR "Missing override nanopb options file: ${override_options}")
    endif()
  endforeach()

  # Decide whether to regenerate.  The manifest records the hash of every input
  # and the pinned tool version, so this is a content comparison: it does not
  # depend on file timestamps, which git does not preserve, and it does not
  # depend on whether a generator happens to be installed.

  set(tools_text "nanopb ${NANOPB_VERSION}")
  input_manifest_text(expected_manifest
                      ROOT "${CMAKE_SOURCE_DIR}"
                      TOOLS "${tools_text}"
                      INPUTS ${manifest_inputs})

  set(committed_manifest "")
  if(EXISTS "${manifest}")
    file(READ "${manifest}" committed_manifest)
  endif()

  set(outputs_present TRUE)
  foreach(_f IN LISTS nanopb_sources nanopb_headers)
    if(NOT EXISTS "${_f}")
      set(outputs_present FALSE)
    endif()
  endforeach()
  if(NOT EXISTS "${default_config}")
    set(outputs_present FALSE)
  endif()

  if(expected_manifest STREQUAL committed_manifest AND outputs_present)
    set(up_to_date TRUE)
  else()
    set(up_to_date FALSE)
  endif()

  set(unbuildable FALSE)
  if(REGENERATE_SOURCES STREQUAL "ON")
    set(regenerate TRUE)
  elseif(REGENERATE_SOURCES STREQUAL "OFF")
    set(regenerate FALSE)
    # A prototype variant has nothing to build under OFF. That is not a reason
    # to fail the whole configure: a release build configures the entire tree
    # and builds only the distributed targets, so the prototypes must be
    # configurable even though they are unbuildable. Failing when one is
    # actually asked for says the same thing at the only moment it matters.
    if(NOT sources_committed OR NOT outputs_present)
      set(unbuildable TRUE)
    endif()
    if(NOT up_to_date AND sources_committed)
      reproducibility_problem(
        "Committed generated sources for ${name} are stale: the inputs recorded "
        "in ${manifest} do not match the current .proto, options, config-gen or "
        "nanopb version. REGENERATE_SOURCES=OFF means they will be built as-is.")
    endif()
  else()
    # AUTO: regenerate exactly when the recorded inputs no longer match.
    if(up_to_date)
      set(regenerate FALSE)
    else()
      set(regenerate TRUE)
    endif()
  endif()

  if(regenerate)
    if(NOT NANOPB_GENERATOR_EXECUTABLE)
      message(FATAL_ERROR
        "${name} needs its generated sources rebuilt, but no nanopb generator "
        "was found. Set NANOPB_ROOT or NANOPB_GENERATOR_EXECUTABLE, or "
        "configure with -DREGENERATE_SOURCES=OFF to build the committed "
        "sources as they are.")
    endif()

    file(MAKE_DIRECTORY "${generated_dir}")

    add_custom_command(
      OUTPUT "${default_config}"
      COMMAND "${CONFIG_GEN_PYTHON}" "${CONFIG_GEN_SCRIPT}"
              --descriptor-set "${CONFIG_GEN_DESCRIPTOR}"
              "${default_config_json}" "${default_config}"
      DEPENDS config-gen-descriptor "${CONFIG_GEN_SCRIPT}" "${default_config_json}"
      VERBATIM)

    foreach(P ${TAG_PROTO_SOURCES})
      cmake_path(GET P PARENT_PATH parent)
      cmake_path(GET P STEM stem)
      set(combined_options "${CMAKE_CURRENT_BINARY_DIR}/${stem}.options")

      add_custom_command(
        OUTPUT "${combined_options}"
        COMMAND "${CMAKE_COMMAND}"
                "-DOUTPUT=${combined_options}"
                "-DINPUTS=${NANOPB_DEFAULT_OPTIONS_DIR}/${stem}.options;${CMAKE_CURRENT_SOURCE_DIR}/${stem}.override.options"
                -P "${CMAKE_SOURCE_DIR}/cmake/CombineFiles.cmake"
        DEPENDS
          "${CMAKE_SOURCE_DIR}/cmake/CombineFiles.cmake"
          "${NANOPB_DEFAULT_OPTIONS_DIR}/${stem}.options"
          "${CMAKE_CURRENT_SOURCE_DIR}/${stem}.override.options"
        VERBATIM)

      add_custom_command(
        OUTPUT "${generated_dir}/${stem}.pb.c" "${generated_dir}/${stem}.pb.h"
        COMMAND "${NANOPB_GENERATOR_EXECUTABLE}"
                -I "${CMAKE_CURRENT_BINARY_DIR}"
                -I "${parent}"
                -D "${generated_dir}"
                "${stem}.proto"
        DEPENDS ${TAG_PROTO_SOURCES} "${combined_options}"
        VERBATIM)
    endforeach()

    # The manifest is written only after the generators have run, so an
    # interrupted or failed build never leaves a manifest claiming freshness.

    # The input list goes through a file: passed as -DINPUTS=<list> it would
    # expand into separate arguments at the command line.
    set(manifest_inputs_file "${CMAKE_CURRENT_BINARY_DIR}/manifest-inputs.txt")
    string(REPLACE ";" "\n" manifest_inputs_lines "${manifest_inputs}")
    file(WRITE "${manifest_inputs_file}" "${manifest_inputs_lines}\n")

    add_custom_command(
      OUTPUT "${manifest}"
      COMMAND "${CMAKE_COMMAND}"
              "-DOUTPUT=${manifest}"
              "-DROOT=${CMAKE_SOURCE_DIR}"
              "-DTOOLS=${tools_text}"
              "-DINPUTS_FILE=${manifest_inputs_file}"
              -P "${CMAKE_SOURCE_DIR}/cmake/InputManifest.cmake"
      DEPENDS ${nanopb_sources} ${nanopb_headers} "${default_config}"
              "${manifest_inputs_file}"
              "${CMAKE_SOURCE_DIR}/cmake/InputManifest.cmake"
      COMMENT "Recording generated-source inputs for ${name}"
      VERBATIM)

    add_custom_target(
      ${name}_proto_sources ALL
      DEPENDS ${nanopb_options} ${nanopb_sources} ${nanopb_headers}
              "${default_config}" "${manifest}")
  elseif(unbuildable)
    add_custom_target(${name}_proto_sources
      COMMAND ${CMAKE_COMMAND} -E echo
              "${name} does not commit its generated sources, and REGENERATE_SOURCES=OFF forbids generating them. Configure with -DREGENERATE_SOURCES=AUTO to build this variant."
      COMMAND ${CMAKE_COMMAND} -E false
      VERBATIM)
  else()
    # Nothing to build: the committed sources are used as they are.  The target
    # still exists because the tag targets depend on it by name.
    add_custom_target(${name}_proto_sources)
  endif()

  add_library(${name}_proto INTERFACE)
  # NANOPB_RUNTIME_DIR carries the vendored pb.h and the pb_*.c sources; the
  # firmware makefiles reach both through NANOPBDIR, which is derived from this
  # property (UINCDIR for the headers, VPATH for the sources).
  target_include_directories(${name}_proto INTERFACE "${generated_dir}"
                                                     ${NANOPB_RUNTIME_DIR})

  target_sources(${name}_proto INTERFACE ${nanopb_sources} "${default_config}")

endfunction()

#add_subdirectory(acceltag-proto-c)
#add_subdirectory(acceltag-ng-proto-c)
add_subdirectory(bittag-proto-c)
add_subdirectory(bittag-legacy-proto-c)
add_subdirectory(bittag-ng-proto-c)
add_subdirectory(prestag-proto-c)
add_subdirectory(prestagraw-proto-c)
add_subdirectory(bitprestag-proto-c)
add_subdirectory(uiuctag-proto-c)
add_subdirectory(compasstag-proto-c)
add_subdirectory(imutag-proto-c)
#add_subdirectory(luxtag-proto-c)
