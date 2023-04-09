cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 20)

# set(CMAKE_BUILD_TYPE Debug)

set(Qt6_DIR "/Users/geobrown/Qt/6.5.0/macos")

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

add_subdirectory(lib)
add_subdirectory(cli)
add_subdirectory(qtmon)
add_subdirectory(btdataviz)

if(WIN32)
  install(
    TARGETS btviz
    DESTINATION .
    COMPONENT btviz)
  install(TARGETS qtmonitor tag-info tag-test tag-test-example tag-dwnld
          DESTINATION .)
else()
  install(TARGETS btviz qtmonitor 
# tag-info tag-test 
# tag-test-example tag-dwnld
          BUNDLE DESTINATION bin)
endif()

set(CPACK_PACKAGE_NAME "NanoTag")
set(CPACK_PACKAGE_VENDOR "cs.indiana.edu")
set(CPACK_PACKAGE_VERSION "1.1.0")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 1)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NanoTag Host Tools")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")


if(WIN32)
  set(CPACK_GENERATOR ${CPACK_BINARY_WIX})
endif()

if(APPLE)
  set(CPACK_GENERATOR ${CPACK_BINARY_DRAGNDROP})
endif()

include(CPack)
