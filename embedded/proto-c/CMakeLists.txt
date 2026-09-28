cmake_minimum_required(VERSION 3.20)

#
# Create a tool for translating json tag configuration files to 
# C code
#

# config-gen translates default-config.json into the proto3 wire encoding.  Its
# output is committed like the nanopb output, so it is only needed when sources
# are regenerated; REGENERATE_SOURCES=OFF is the mode that builds firmware
# without it.

if(NOT REGENERATE_SOURCES STREQUAL "OFF")
  add_executable(config-gen config-gen.cc)
  target_compile_features(config-gen PRIVATE cxx_std_20)
  target_link_libraries(config-gen PRIVATE proto)
endif()

# Captured at file scope: inside a function CMAKE_CURRENT_LIST_DIR is the
# CALLER's directory, not this file's, and add_nanopb_target is called from each
# variant's subdirectory.
set(NANOPB_PROTO_C_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(NANOPB_DEFAULT_OPTIONS_DIR "${NANOPB_PROTO_C_DIR}/default-options")

include("${CMAKE_SOURCE_DIR}/cmake/InputManifest.cmake")

#
# Helper function to create nanopb pb.h pb.c files
#
function(add_nanopb_target name)

  get_target_property(TAG_PROTO_SOURCES tag-proto-sources INTERFACE_SOURCES)

  # Generated sources are committed here so that an ordinary firmware build
  # needs neither the nanopb generator nor config-gen.  Regeneration writes back
  # into this directory rather than the build tree, so what is built is what is
  # committed.

  set(generated_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated")
  set(manifest "${generated_dir}/inputs.sha256")
  set(default_config "${generated_dir}/default_config.c")
  set(default_config_json "${CMAKE_CURRENT_SOURCE_DIR}/default-config.json")

  set(nanopb_sources)
  set(nanopb_headers)
  set(nanopb_options)
  set(manifest_inputs
      "${default_config_json}"
      "${NANOPB_PROTO_C_DIR}/config-gen.cc"
      "${CMAKE_SOURCE_DIR}/cmake/CombineFiles.cmake")
  list(APPEND manifest_inputs ${TAG_PROTO_SOURCES})

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
    list(APPEND manifest_inputs "${default_options}" "${override_options}")
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

  if(REGENERATE_SOURCES STREQUAL "ON")
    set(regenerate TRUE)
  elseif(REGENERATE_SOURCES STREQUAL "OFF")
    set(regenerate FALSE)
    if(NOT outputs_present)
      message(FATAL_ERROR
        "REGENERATE_SOURCES=OFF but ${name} has no committed generated sources "
        "under ${generated_dir}. Configure with -DREGENERATE_SOURCES=ON once to "
        "produce them.")
    endif()
    if(NOT up_to_date)
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
      COMMAND $<TARGET_FILE:config-gen> "${default_config_json}" "${default_config}"
      DEPENDS config-gen "${default_config_json}"
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
