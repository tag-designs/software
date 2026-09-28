cmake_minimum_required(VERSION 3.10)

# Tools for building embedded code

find_program(GCCARM arm-none-eabi-gcc REQUIRED)
find_program(MAKE_EXECUTABLE make REQUIRED)
if(MSVC)
  find_program(FMPP fmpp fmpp.bat PATHS "c:/Program Files/fmpp/bin" REQUIRED)
else()
  find_program(FMPP fmpp REQUIRED)
endif()

set(_repo_chibios_dir "${CMAKE_SOURCE_DIR}/ChibiOS")
set(CHIBIOS_DIR "" CACHE PATH "ChibiOS source directory")
if(NOT CHIBIOS_DIR)
  if(EXISTS "${_repo_chibios_dir}/os")
    set(CHIBIOS_DIR "${_repo_chibios_dir}" CACHE PATH "ChibiOS source directory" FORCE)
  elseif(DEFINED ENV{CHIBIOS_DIR} AND EXISTS "$ENV{CHIBIOS_DIR}/os")
    message(STATUS
      "Using CHIBIOS_DIR from the environment; initialize the ChibiOS submodule for reproducible builds")
    set(CHIBIOS_DIR "$ENV{CHIBIOS_DIR}" CACHE PATH "ChibiOS source directory" FORCE)
  else()
    message(FATAL_ERROR
      "ChibiOS was not found. Run `git submodule update --init --recursive ChibiOS`, "
      "or configure with -DCHIBIOS_DIR=/path/to/ChibiOS.")
  endif()
endif()
if(NOT EXISTS "${CHIBIOS_DIR}/os")
  message(FATAL_ERROR "CHIBIOS_DIR does not look like a ChibiOS source tree: ${CHIBIOS_DIR}")
endif()
message(STATUS "CHIBIOS_DIR is ${CHIBIOS_DIR}")

# Tools for programming hardware

if(MSVC)
  find_program(
    STM32_PROGRAMMER
    NAMES STM32_Programmer_CLI STM32_Programmer_CLI.exe
    PATHS
      "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin")
else()
  find_program(
    STM32_PROGRAMMER
    NAMES STM32_Programmer_CLI
    HINTS
      "$ENV{STM32_PRG_PATH}"
      "/Users/geobrown/Software/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin"
      "/Users/geobrown/Software/stm32cubeprogrammer/STM32CubeProgrammer.app/Contents/MacOs/bin"
    PATHS "/home/geobrown/Software/STM32CubeProgrammer/bin"
  )
endif()

if(NOT STM32_PROGRAMMER)
  message(STATUS
    "To program embedded boards install https://www.st.com/en/development-tools/stm32cubeprog.html"
  )
endif()

find_program(PYTHON_EXECUTABLE NAMES python3 python)
set(STM32_PROGRAMMER_PROBE
    "pid:0483:3748"
    CACHE STRING
          "ST-LINK selector for download/erase targets: pid:0483:3748, auto, prompt, serial:<sn>, or index:<n>")
set(STM32_PROGRAMMER_SELECT_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/tools/stm32_programmer_select.py")

#
# Function to build embedded ChibiOS targets using make
#

function(add_embedded_target name proto_target)

  # DISTRIBUTE marks a tag as actively deployed: its firmware is installed and
  # packaged, and it is the set the reproducibility machinery applies to.  Tags
  # without it are development prototypes -- they must still compile, but no
  # promise is made that a build from a future checkout reproduces them.  The
  # remaining arguments are passed through to make as before.

  cmake_parse_arguments(ARG "DISTRIBUTE" "" "" ${ARGN})
  set(make_vars ${ARG_UNPARSED_ARGUMENTS})

  if(ARG_DISTRIBUTE)
    set_property(GLOBAL APPEND PROPERTY ULTRALIGHT_DISTRIBUTED_TAGS ${name})
    if(NOT proto_target STREQUAL "0")
      set_property(GLOBAL APPEND PROPERTY ULTRALIGHT_DISTRIBUTED_PROTO_TARGETS
                                          ${proto_target})
    endif()
  endif()

  # get some paths

  message(STATUS "Embedded target ${name} proto target: ${proto_target}")

  set(tag_source_dir
      ${CMAKE_CURRENT_LIST_DIR}/..
      CACHE INTERNAL "")

  get_property(
    monitor_includes
    TARGET tag_monitor_interface
    PROPERTY INTERFACE_INCLUDE_DIRECTORIES)

  # create variables for gnu make

  set(boarddir ${CMAKE_CURRENT_BINARY_DIR}/../../boards)
  set(builddir ${CMAKE_CURRENT_BINARY_DIR}/build)
  set(depdir ${CMAKE_CURRENT_BINARY_DIR}/dep)

  # these just affect tags with nanopb

  set(protopaths "")
  if(NOT proto_target STREQUAL "0")
    if(NOT TARGET ${proto_target})
      message(FATAL_ERROR "Embedded target ${name} references missing proto target ${proto_target}")
    endif()
    get_target_property(protopaths ${proto_target}
                        INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  # get_target_property(configdir default_config BINARY_DIR)

  # patch the source directory to get relative path to current source this is
  # used inside the tags for information about the relative path to the tag
  # source within the git repo.

  string(REPLACE ${CMAKE_SOURCE_DIR} "" src_path ${CMAKE_CURRENT_SOURCE_DIR})

  # the following only in recent versions of cmake cmake_path(RELATIVE_PATH
  # src_path ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

  message(STATUS "Current Source Dir ${src_path}")

  # create options for make

  set(default_opt
      BUILDDIR=${builddir}
      DEPDIR=${depdir}
      NANOPBDIR="${protopaths}"
      BOARDDIR=${boarddir}
      CHIBIOS=${CHIBIOS_DIR}
      SOURCEDIR=${src_path}
      PROJECT=${name}
      MONITORINCDIR=\"${monitor_includes}\")

  # create build target -- depends upon binary file

  set(target_build_dir ${CMAKE_CURRENT_BINARY_DIR}/build)
  set(target_elf ${target_build_dir}/${name}.elf)
  set(target_hex ${target_build_dir}/${name}.hex)
  set(target_bin ${target_build_dir}/${name}.bin)
  set(target_dmp ${target_build_dir}/${name}.dmp)
  set(target_list ${target_build_dir}/${name}.list)
  set(
    target_firmware_artifacts
    ${target_elf}
    ${target_hex}
    ${target_bin}
    ${target_dmp}
    ${target_list})

  #add_custom_target(${name} DEPENDS ${target_elf})

  add_custom_target(${name}
    BYPRODUCTS ${target_firmware_artifacts}
    #COMMAND echo " ${default_opt} ${ARGN}"
    COMMAND ${MAKE_EXECUTABLE} -C ${name} ${default_opt} ${make_vars}
    WORKING_DIRECTORY ${tag_source_dir}
    COMMENT "Building ${name}")

  # build version.h file which includes git variables

  add_custom_target(${name}_version_h ${CMAKE_COMMAND} -P
                                      ${CMAKE_SOURCE_DIR}/cmake/version.cmake
                    BYPRODUCTS version.h)

  add_dependencies(${name} ${name}_version_h)

  set_property(TARGET ${name} PROPERTY ADDITIONAL_CLEAN_FILES
                                       ${depdir};${builddir})

  #
  # add proto files as dependency for tags
  #

  if(NOT proto_target STREQUAL "0")
    add_dependencies(${name} ${proto_target}_sources)
  endif()

  # create download target

  if(STM32_PROGRAMMER AND PYTHON_EXECUTABLE)
    add_custom_target(
      ${name}-download
      DEPENDS ${name}
      COMMAND
        ${PYTHON_EXECUTABLE} ${STM32_PROGRAMMER_SELECT_SCRIPT}
        --programmer ${STM32_PROGRAMMER}
        --selector ${STM32_PROGRAMMER_PROBE}
        --
        -d ${target_elf}
        -g 0x08000000
      VERBATIM)

    add_custom_target(
      ${name}-erase
      COMMAND
        ${PYTHON_EXECUTABLE} ${STM32_PROGRAMMER_SELECT_SCRIPT}
        --programmer ${STM32_PROGRAMMER}
        --selector ${STM32_PROGRAMMER_PROBE}
        --
        -e all
      VERBATIM)

    add_custom_target(
      ${name}-dfu
      DEPENDS ${name}
      COMMAND ${STM32_PROGRAMMER} -c port=usb1 -d ${target_elf}
      VERBATIM)
  elseif(STM32_PROGRAMMER)
    message(STATUS
      "Python was not found; embedded download targets will use STM32CubeProgrammer without probe selection"
    )
    add_custom_target(
      ${name}-download
      DEPENDS ${name}
      COMMAND ${STM32_PROGRAMMER} -c port=SWD mode=UR reset=HWrst -d
              ${target_elf} -g 0x08000000
      VERBATIM)

    add_custom_target(${name}-erase COMMAND ${STM32_PROGRAMMER} -c port=SWD
                                            mode=UR reset=HWrst -e all VERBATIM)

    add_custom_target(
      ${name}-dfu
      DEPENDS ${name}
      COMMAND ${STM32_PROGRAMMER} -c port=usb1 -d ${target_elf}
      VERBATIM)
  endif()

  # Only actively deployed tags are installed; prototypes are built but not
  # shipped.

  if(ARG_DISTRIBUTE)
    install(
      FILES ${target_firmware_artifacts}
      DESTINATION "${CMAKE_INSTALL_DATADIR}/${CPACK_PACKAGE_NAME}/firmware/${name}"
      OPTIONAL)
  endif()

endfunction()

# Make the programmer bases and tags

add_subdirectory(boards)
add_subdirectory(bases)
add_subdirectory(proto-c)
add_subdirectory(tags)

#
# Actively deployed tags
#
# Tags are added above; those marked DISTRIBUTE are the ones in field use whose
# firmware is installed and packaged.  The set of proto-c variants that need the
# same treatment is derived from them rather than listed separately, so the two
# cannot drift apart.
#

get_property(_distributed_tags GLOBAL PROPERTY ULTRALIGHT_DISTRIBUTED_TAGS)
get_property(_distributed_protos GLOBAL PROPERTY ULTRALIGHT_DISTRIBUTED_PROTO_TARGETS)
list(REMOVE_DUPLICATES _distributed_protos)
list(SORT _distributed_tags)
list(SORT _distributed_protos)
set_property(GLOBAL PROPERTY ULTRALIGHT_DISTRIBUTED_PROTO_TARGETS ${_distributed_protos})

if(NOT _distributed_tags)
  message(WARNING
    "No embedded target is marked DISTRIBUTE; no firmware will be installed. "
    "Mark the actively deployed tags in embedded/tags/*/CMakeLists.txt.")
else()
  list(JOIN _distributed_tags " " _distributed_tags_text)
  list(JOIN _distributed_protos " " _distributed_protos_text)
  message(STATUS "Distributed tags: ${_distributed_tags_text}")
  message(STATUS "Distributed proto targets: ${_distributed_protos_text}")
endif()
