cmake_minimum_required(VERSION 3.15)

project(NanoTag VERSION 0.1.0)
#set(VCPKG_VERBOSE true)

set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/")
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

set(CMAKE_FIND_ROOT 
    "/home/geobrown/Software;/Users/geobrown/Software;c:/Program Files/fmpp/bin;C:/Program Files/STMicroelectronics" )

set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

add_subdirectory(software)
add_subdirectory(hardware)
add_subdirectory(docs)

set(CPACK_PACKAGE_NAME "UltralightTags")
set(CPACK_PACKAGE_VENDOR "cs.indiana.edu")
set(CPACK_PACKAGE_VERSION "1.1.0")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 1)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ultralight Tags Host Tools")



if(WIN32)
  set(CPACK_GENERATOR ${CPACK_BINARY_WIX})
endif()

if(APPLE)
  set(CPACK_GENERATOR ${CPACK_BINARY_DRAGNDROP})
endif()

include(CPack)



