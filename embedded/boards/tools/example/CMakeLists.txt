cmake_minimum_required(VERSION 3.15)

#[[
  @brief  Standalone board-generation example for an STM32L432 board.

  @details Runs generate_board_chcfg.py against ChibiOS' stm32l4xx board XML
           template, configures the matching fmpp input file, and then renders
           board.c, board.h, board.mk, and board_standby.h into this example's
           build tree. This file is intentionally self-contained so it can be
           copied into another project beside the board tools.
]]
project(stm32l432_board_generation_example NONE)

set(CHIBIOS_DIR
    ""
    CACHE PATH "Path to a ChibiOS source tree containing os/ and tools/ftl/")
set(FMPP_EXECUTABLE
    ""
    CACHE FILEPATH "Path to the fmpp executable used by ChibiOS board templates")

if(NOT CHIBIOS_DIR AND DEFINED ENV{CHIBIOS_DIR})
  file(TO_CMAKE_PATH "$ENV{CHIBIOS_DIR}" CHIBIOS_DIR)
  set(CHIBIOS_DIR
      "${CHIBIOS_DIR}"
      CACHE PATH "Path to a ChibiOS source tree containing os/ and tools/ftl/"
      FORCE)
endif()

if(NOT FMPP_EXECUTABLE AND DEFINED ENV{FMPP_EXECUTABLE})
  file(TO_CMAKE_PATH "$ENV{FMPP_EXECUTABLE}" FMPP_EXECUTABLE)
  set(FMPP_EXECUTABLE
      "${FMPP_EXECUTABLE}"
      CACHE FILEPATH "Path to the fmpp executable used by ChibiOS board templates"
      FORCE)
endif()

if(NOT FMPP_EXECUTABLE)
  find_program(_FMPP_EXECUTABLE NAMES fmpp fmpp.bat)
  if(_FMPP_EXECUTABLE)
    set(FMPP_EXECUTABLE
        "${_FMPP_EXECUTABLE}"
        CACHE FILEPATH "Path to the fmpp executable used by ChibiOS board templates"
        FORCE)
  endif()
endif()

if(NOT CHIBIOS_DIR)
  message(FATAL_ERROR
    "CHIBIOS_DIR is not set.\n"
    "Point it at a ChibiOS checkout when configuring this example:\n"
    "  cmake -S ${CMAKE_CURRENT_SOURCE_DIR} -B build "
    "-DCHIBIOS_DIR=/path/to/ChibiOS "
    "-DFMPP_EXECUTABLE=/path/to/fmpp\n"
    "You may also set the CHIBIOS_DIR environment variable before running CMake.")
endif()

if(NOT EXISTS "${CHIBIOS_DIR}/os")
  message(FATAL_ERROR
    "CHIBIOS_DIR does not look like a ChibiOS source tree: ${CHIBIOS_DIR}\n"
    "Expected to find: ${CHIBIOS_DIR}/os\n"
    "Configure with:\n"
    "  cmake -S ${CMAKE_CURRENT_SOURCE_DIR} -B build "
    "-DCHIBIOS_DIR=/path/to/ChibiOS "
    "-DFMPP_EXECUTABLE=/path/to/fmpp")
endif()

if(NOT FMPP_EXECUTABLE)
  message(FATAL_ERROR
    "FMPP_EXECUTABLE is not set and fmpp was not found on PATH.\n"
    "Install fmpp, then configure this example with:\n"
    "  cmake -S ${CMAKE_CURRENT_SOURCE_DIR} -B build "
    "-DCHIBIOS_DIR=${CHIBIOS_DIR} "
    "-DFMPP_EXECUTABLE=/path/to/fmpp\n"
    "You may also put fmpp on PATH or set the FMPP_EXECUTABLE environment variable.")
endif()

if(NOT EXISTS "${FMPP_EXECUTABLE}")
  message(FATAL_ERROR
    "FMPP_EXECUTABLE does not exist: ${FMPP_EXECUTABLE}\n"
    "Configure with the full path to fmpp, for example:\n"
    "  cmake -S ${CMAKE_CURRENT_SOURCE_DIR} -B build "
    "-DCHIBIOS_DIR=${CHIBIOS_DIR} "
    "-DFMPP_EXECUTABLE=/path/to/fmpp")
endif()

find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)

set(BOARD_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(BOARD_TYPE STM32L432_Standalone_Example)
set(PROCESSOR stm32l4xx)
set(PROCESSOR_XML stm32l4)

set(CHIBIOS_XML_TEMPLATE
    "${CHIBIOS_DIR}/tools/ftl/xml/${PROCESSOR_XML}board.xml")
set(CHIBIOS_FTL_TEMPLATE_DIR
    "${CHIBIOS_DIR}/tools/ftl/processors/boards/${PROCESSOR}/templates")
set(CHIBIOS_FTL_LIBS_DIR "${CHIBIOS_DIR}/tools/ftl/libs")

if(NOT EXISTS "${CHIBIOS_XML_TEMPLATE}")
  message(FATAL_ERROR
    "Missing ChibiOS board XML template: ${CHIBIOS_XML_TEMPLATE}\n"
    "Check that CHIBIOS_DIR points at a ChibiOS tree with tools/ftl/xml.")
endif()

if(NOT EXISTS "${CHIBIOS_FTL_TEMPLATE_DIR}/board.c.ftl")
  message(FATAL_ERROR
    "Missing ChibiOS stm32l4xx board templates: ${CHIBIOS_FTL_TEMPLATE_DIR}\n"
    "Check that CHIBIOS_DIR contains tools/ftl/processors/boards/stm32l4xx/templates.")
endif()

if(NOT EXISTS "${CHIBIOS_FTL_LIBS_DIR}")
  message(FATAL_ERROR
    "Missing ChibiOS FreeMarker helper library directory: ${CHIBIOS_FTL_LIBS_DIR}")
endif()

set(GENERATED_CFG_DIR "${CMAKE_CURRENT_BINARY_DIR}/cfg")
set(GENERATED_BOARD_DIR "${CMAKE_CURRENT_BINARY_DIR}/boards/${BOARD_TYPE}")
set(GENERATED_CHCFG "${GENERATED_CFG_DIR}/board.chcfg")
set(GENERATED_FMPP "${GENERATED_CFG_DIR}/board.fmpp")
set(GENERATED_STANDBY_HEADER "${GENERATED_BOARD_DIR}/board_standby.h")
set(CUSTOMIZATIONS "${CMAKE_CURRENT_SOURCE_DIR}/cfg/board-customizations.json")

file(MAKE_DIRECTORY "${GENERATED_CFG_DIR}" "${GENERATED_BOARD_DIR}")

set(BOARD_FMPP_SOURCE_ROOT "${CHIBIOS_FTL_TEMPLATE_DIR}")
set(BOARD_FMPP_OUTPUT_ROOT "${GENERATED_BOARD_DIR}")
set(BOARD_FMPP_DATA_ROOT "${GENERATED_CFG_DIR}")
configure_file(
  "${BOARD_TOOLS_DIR}/board.fmpp.in"
  "${GENERATED_FMPP}"
  @ONLY)

add_custom_command(
  OUTPUT
    "${GENERATED_CHCFG}"
    "${GENERATED_STANDBY_HEADER}"
  COMMAND "${PYTHON_EXECUTABLE}"
          "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
          --template "${CHIBIOS_XML_TEMPLATE}"
          --customizations "${CUSTOMIZATIONS}"
          --output "${GENERATED_CHCFG}"
          --standby-header "${GENERATED_STANDBY_HEADER}"
  DEPENDS
    "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
    "${CUSTOMIZATIONS}"
    "${CHIBIOS_XML_TEMPLATE}"
  VERBATIM
  COMMENT "Generating ${BOARD_TYPE}/board.chcfg")

set(GENERATED_BOARD_FILES
  "${GENERATED_BOARD_DIR}/board.c"
  "${GENERATED_BOARD_DIR}/board.h"
  "${GENERATED_BOARD_DIR}/board.mk")

add_custom_command(
  OUTPUT ${GENERATED_BOARD_FILES}
  COMMAND "${FMPP_EXECUTABLE}"
          --freemarker-links "{lib : ${CHIBIOS_FTL_LIBS_DIR}}"
          -C "${GENERATED_FMPP}"
          -v
          -O "${GENERATED_BOARD_DIR}"
  DEPENDS
    "${GENERATED_CHCFG}"
    "${GENERATED_STANDBY_HEADER}"
    "${GENERATED_FMPP}"
    "${CHIBIOS_FTL_TEMPLATE_DIR}/board.c.ftl"
    "${CHIBIOS_FTL_TEMPLATE_DIR}/board.h.ftl"
    "${CHIBIOS_FTL_TEMPLATE_DIR}/board.mk.ftl"
  VERBATIM
  COMMENT "Rendering ChibiOS board files for ${BOARD_TYPE}")

add_custom_target(generate-stm32l432-example-board
  ALL
  DEPENDS ${GENERATED_BOARD_FILES} "${GENERATED_STANDBY_HEADER}")

message(STATUS "Generated board output: ${GENERATED_BOARD_DIR}")
