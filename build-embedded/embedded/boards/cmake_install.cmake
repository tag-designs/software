# Install script for directory: /Users/geobrown/Research/tag-designs/software/embedded/boards

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/Users/geobrown/Research/tag-designs/software/build-embedded/install")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/bittag-base-jlcpcb-v3/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/tag-breakout-base-jlcpcb32-v1/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/tag-breakout-base-l432-v1/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/bittag-base-v7/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/tag-base-c071-v1/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/BitTagv6/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/TagSteval/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/PresTagv3/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/BitPresTagv1/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/CompassTagv1/cmake_install.cmake")
  include("/Users/geobrown/Research/tag-designs/software/build-embedded/embedded/boards/IMUTagv1/cmake_install.cmake")

endif()

