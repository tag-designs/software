set(TARGET_NAME qfi)
cmake_minimum_required(VERSION 3.15)

message("Configure qfi")
# Find includes in corresponding build directories

################################################################################

if(WIN32)
    if(BUILD_SHARED_LIBS)
        add_definitions(-DQFI_DLL_EXPORTS)
    endif()
endif()

################################################################################

set(CPP_FILES
    qfi_AI.cpp
    qfi_ALT.cpp
    qfi_ASI.cpp
    qfi_Colors.cpp
    qfi_EADI.cpp
    qfi_EHSI.cpp
    qfi_Fonts.cpp
    qfi_HI.cpp
    qfi_TC.cpp
    qfi_VSI.cpp
)

set(MOC_SRC_FILES
    qfi_AI.h
    qfi_ALT.h
    qfi_ASI.h
    qfi_EADI.h
    qfi_EHSI.h
    qfi_HI.h
    qfi_TC.h
    qfi_VSI.h
)

set(RCC_SRC_FILES
    qfi.qrc
)

set(SOURCES ${CPP_FILES} ${MOC_SRC_FILES} ${RCC_SRC_FILES})

set(HEADERS
    ${MOC_SRC_FILES}
    qfi_defs.h
    qfi_doxygen.h
    qfi_Colors.h
    qfi_Fonts.h
)

find_package(Qt6 COMPONENTS Core Widgets Svg CONFIG)   #DataVisualization
if(NOT Qt6_FOUND)
  message("Qt6 Not found")
  message("Cannot Build qtcalibrate")
  return()
endif()


################################################################################

add_library(${TARGET_NAME} ${SOURCES})

################################################################################

target_link_libraries(${TARGET_NAME} Qt6::Core Qt6::Widgets Qt6::Svg)

################################################################################

set_target_properties(${TARGET_NAME} PROPERTIES
    VERSION ${QFI_VERSION_STRING}
    SOVERSION ${QFI_VERSION_MAJOR}
    PUBLIC_HEADER "${HEADERS}"
)

install(TARGETS ${TARGET_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qfi
)
