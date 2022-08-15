# Findlibstlink.cmake
# Find and install external libstlink library

# Once done this will define
#
#  LIBSTLINK_FOUND         libstlink present on system
#  LIBSTLINK_INCLUDE_DIR   the libstlink include directory
#  LIBSTLINK_LIBRARY       the libraries needed to use libstlink


FIND_PATH(
    LIBSTLINK_INCLUDE_DIR NAMES stlink.h
    HINTS /usr/include /usr/local/include /opt/include
)

set(LIBSTLINK_NAME "stlink")
FIND_LIBRARY(
    LIBSTLINK_LIBRARY NAMES ${LIBSTLINK_NAME}
    HINTS /usr/ /usr/local/ /opt
)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(libstlink DEFAULT_MSG LIBSTLINK_LIBRARY LIBSTLINK_INCLUDE_DIR)
mark_as_advanced(LIBSTLINK_INCLUDE_DIR LIBSTLINK_LIBRARY)

set(LIBSTLINK_INCLUDE_DIR ${LIBSTLINK_INCLUDE_DIR} ${LIBSTLINK_INCLUDE_DIR}/stlink)

if (NOT LIBSTLINK_FOUND)
        message(FATAL_ERROR "Expected libslink library not found on your system! Verify your system integrity.")
endif ()