cmake_minimum_required(VERSION 3.20)

#
# Create a tool for translating json tag configuration files to 
# C code
#

add_executable(config-gen config-gen.cc)
target_compile_features(config-gen PRIVATE cxx_std_20)
target_link_libraries(config-gen PRIVATE proto)

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
  set(nanopb_options
      "${CMAKE_CURRENT_SOURCE_DIR}/tag.options"
      "${CMAKE_CURRENT_SOURCE_DIR}/tagdata.options")

  foreach(P ${TAG_PROTO_SOURCES})
    cmake_path(GET P PARENT_PATH parent)
    cmake_path(GET P STEM stem)
    set(generated_source "${CMAKE_CURRENT_BINARY_DIR}/${stem}.pb.c")
    set(generated_header "${CMAKE_CURRENT_BINARY_DIR}/${stem}.pb.h")
    list(APPEND nanopb_sources "${generated_source}")
    list(APPEND nanopb_headers "${generated_header}")

    add_custom_command(
      OUTPUT "${generated_source}" "${generated_header}"
      COMMAND "${NANOPB_GENERATOR_SOURCE_DIR}/nanopb_generator"
              -I "${CMAKE_CURRENT_SOURCE_DIR}"
              -I "${parent}"
              -D "${CMAKE_CURRENT_BINARY_DIR}"
              "${stem}.proto"
      DEPENDS ${TAG_PROTO_SOURCES} ${nanopb_options}
      VERBATIM)
  endforeach()

  add_custom_target(
    ${name}_proto_sources ALL
    DEPENDS ${nanopb_sources} ${nanopb_headers} "${default_config}")

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
