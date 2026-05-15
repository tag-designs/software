cmake_minimum_required(VERSION 3.20)

#
# Create a tool for translating json tag configuration files to 
# C code
#

add_executable(config-gen config-gen.cc)
target_compile_features(config-gen PRIVATE cxx_std_20)
target_link_libraries(config-gen PRIVATE proto)

set(NANOPB_DEFAULT_OPTIONS_DIR "${CMAKE_CURRENT_LIST_DIR}/default-options")

#
# Helper function to create nanopb pb.h pb.c files
#
function(add_nanopb_target name)

  get_target_property(TAG_PROTO_SOURCES tag-proto-sources INTERFACE_SOURCES)

  # generate default config

  set(default_config "${CMAKE_CURRENT_BINARY_DIR}/default_config.c")
  add_custom_command(
    OUTPUT "${default_config}"
    COMMAND $<TARGET_FILE:config-gen>
            "${CMAKE_CURRENT_SOURCE_DIR}/default-config.json"
            "${default_config}"
    DEPENDS config-gen "${CMAKE_CURRENT_SOURCE_DIR}/default-config.json"
    VERBATIM)

  #  Generate nanopb files

  set(nanopb_sources)
  set(nanopb_headers)
  set(nanopb_options)

  foreach(P ${TAG_PROTO_SOURCES})
    cmake_path(GET P PARENT_PATH parent)
    cmake_path(GET P STEM stem)
    set(generated_source "${CMAKE_CURRENT_BINARY_DIR}/${stem}.pb.c")
    set(generated_header "${CMAKE_CURRENT_BINARY_DIR}/${stem}.pb.h")
    set(default_options "${NANOPB_DEFAULT_OPTIONS_DIR}/${stem}.options")
    set(override_options "${CMAKE_CURRENT_SOURCE_DIR}/${stem}.override.options")
    set(combined_options "${CMAKE_CURRENT_BINARY_DIR}/${stem}.options")
    list(APPEND nanopb_sources "${generated_source}")
    list(APPEND nanopb_headers "${generated_header}")
    list(APPEND nanopb_options "${combined_options}")

    if(NOT EXISTS "${default_options}")
      message(FATAL_ERROR "Missing default nanopb options file: ${default_options}")
    endif()

    if(NOT EXISTS "${override_options}")
      message(FATAL_ERROR "Missing override nanopb options file: ${override_options}")
    endif()

    add_custom_command(
      OUTPUT "${combined_options}"
      COMMAND "${CMAKE_COMMAND}"
              "-DOUTPUT=${combined_options}"
              "-DINPUTS=${default_options};${override_options}"
              -P "${CMAKE_SOURCE_DIR}/cmake/CombineFiles.cmake"
      DEPENDS
        "${CMAKE_SOURCE_DIR}/cmake/CombineFiles.cmake"
        "${default_options}"
        "${override_options}"
      VERBATIM)

    add_custom_command(
      OUTPUT "${generated_source}" "${generated_header}"
      COMMAND "${NANOPB_GENERATOR_SOURCE_DIR}/nanopb_generator"
              -I "${CMAKE_CURRENT_BINARY_DIR}"
              -I "${parent}"
              -D "${CMAKE_CURRENT_BINARY_DIR}"
              "${stem}.proto"
      DEPENDS ${TAG_PROTO_SOURCES} "${combined_options}"
      VERBATIM)
  endforeach()

  add_custom_target(
    ${name}_proto_sources ALL
    DEPENDS
      ${nanopb_options}
      ${nanopb_sources}
      ${nanopb_headers}
      "${default_config}")

  add_library(${name}_proto INTERFACE)
  target_include_directories(${name}_proto INTERFACE ${CMAKE_CURRENT_BINARY_DIR}
                                                     ${NANOPB_SRC_ROOT_FOLDER})

  target_sources(${name}_proto INTERFACE ${nanopb_sources} "${default_config}")

endfunction()

#add_subdirectory(acceltag-proto-c)
#add_subdirectory(acceltag-ng-proto-c)
add_subdirectory(bittag-proto-c)
add_subdirectory(bittag-ng-proto-c)
add_subdirectory(prestag-proto-c)
add_subdirectory(bitprestag-proto-c)
add_subdirectory(compasstag-proto-c)
add_subdirectory(imutag-proto-c)
#add_subdirectory(luxtag-proto-c)
