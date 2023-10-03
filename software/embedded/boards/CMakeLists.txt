cmake_minimum_required(VERSION 3.10)

# function to generate board.[c|h|mk] files with fmpp from configuration

function(generate_board_files name)
  set(BOARD_FILES board.h board.c board.mk)

  add_custom_command(
    OUTPUT ${BOARD_FILES}
    COMMAND ${FMPP} --freemarker-links "{lib : ${CHIBIOS_DIR}/tools/ftl/libs}"
            -C board.fmpp -v -O ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cfg
    DEPENDS cfg/board.c.ftl cfg/board.chcfg cfg/board.fmpp cfg/board.h.ftl
    #COMMENT "Building ${BOARD_FILES}"
    #COMMENT
    #  "${FMPP} --freemarker-links \"{lib : ${CHIBIOS_DIR}/tools/ftl/libs}\" -C board.fmpp -v -O ${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_custom_target(${name} DEPENDS ${BOARD_FILES})
endfunction()

# bases

#add_subdirectory(bittag-base-v5a)
#add_subdirectory(bittag-base-v5)
#add_subdirectory(bittag-base-jlcpcb-v2)
add_subdirectory(bittag-base-jlcpcb-v3)
add_subdirectory(tag-breakout-base-jlcpcb32-v1)
add_subdirectory(bittag-base-v7)

# tags

add_subdirectory(BitTagv5)
add_subdirectory(BitTagv6)
add_subdirectory(TagSteval)
add_subdirectory(PresTagv3)
#add_subdirectory(BitTagNG-proto)
add_subdirectory(BitTagNG-steval)
