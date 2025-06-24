cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(add_nanopb_target name)

  include_directories(${NANOPB_PROTO})
  include_directories(${Protobuf_INCLUDE_DIRS})
  include_directories(${NANOPB_INCLUDE_DIRS})

  get_target_property(TAG_PROTO_SOURCES tag-proto-sources INTERFACE_SOURCES)

  #message("sources ${TAG_PROTO_SOURCES}")
 
  #set(NANOPB_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}"
  #                   --experimental_allow_proto3_optional)
  #set(NANOPB_DEPENDS tag.options tagdata.options)

  # generate default config

  add_custom_command(
    OUTPUT default_config.c
    COMMAND config-gen ${CMAKE_CURRENT_SOURCE_DIR}/default-config.json >
            default_config.c
    DEPENDS config-gen default-config.json)

  #  Generate nanopb files

  foreach(P ${TAG_PROTO_SOURCES})
    cmake_path(GET P PARENT_PATH parent)
    cmake_path(GET P STEM stem)
    #message("add ${stem}.pb.c ${NANOPB_GENERATOR_SOURCE_DIR}/nanopb_generator -I ${CMAKE_CURRENT_SOURCE_DIR} -D ${CMAKE_CURRENT_BINARY_DIR} ${P}" )
    add_custom_command(
      OUTPUT ${stem}.pb.c ${stem}.pb.h
      COMMAND ${NANOPB_GENERATOR_SOURCE_DIR}/nanopb_generator -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${parent} -D ${CMAKE_CURRENT_BINARY_DIR} ${stem}.proto
      DEPENDS ${TAG_PROTO_SOURCES} tag.options tagdata.options
    )
  endforeach()

  add_custom_target(
    ${name}_proto_sources ALL
    DEPENDS ${NANOPROTO_SRCS} ${NANOPROTO_HDRS} tag.options tagdata.options
            default_config.c tagdata.pb.c tag.pb.c)

  add_library(${name}_proto INTERFACE)
  target_include_directories(${name}_proto INTERFACE ${CMAKE_CURRENT_BINARY_DIR}
                                                     ${NANOPB_SRC_ROOT_FOLDER})

  target_sources(${name}_proto INTERFACE ${NANOPROTO_SRCS})

endfunction()

#add_subdirectory(acceltag-proto-c)
#add_subdirectory(acceltag-ng-proto-c)
add_subdirectory(bittag-proto-c)
add_subdirectory(bittag-ng-proto-c)
add_subdirectory(prestag-proto-c)
add_subdirectory(bitprestag-proto-c)
#add_subdirectory(luxtag-proto-c)
