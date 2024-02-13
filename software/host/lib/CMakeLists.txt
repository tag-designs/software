cmake_minimum_required(VERSION 3.15)

message("Configure Tag Library")
include(CMakePrintHelpers)

#
# Find libusb
#

if (MSVC)
     find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
     find_library(libusb libusb-1.0 REQUIRED)
else()
     #find_package(libusb-1.0 REQUIRED)
endif()




find_package(PkgConfig REQUIRED)
pkg_check_modules(libusb REQUIRED libusb-1.0)
find_package(libusb-1.0)
cmake_print_variables(LIBUSB_INCLUDES)

#
# Create tag library 
#

add_library(tag STATIC)

target_sources(tag
    PRIVATE
         tagmonitor.cc 
         tagclass.cc   
         taglogs.cc 
         linkadapt.cc
         log.c
         linkadapt.h
    PUBLIC
         log.h
         tagmonitor.h
         tagclass.h
         taglogs.h
         linkadapt.h
   )

#
# add include directories, libraries to tag library 
#

target_include_directories(tag PRIVATE ${LIBUSB_INCLUDE_DIR})
target_include_directories(tag PRIVATE /usr/include/libusb-1.0)
target_include_directories(tag PRIVATE /usr/local/include/libusb-1.0)
target_include_directories(tag PRIVATE /opt/local/include/libusb-1.0)
target_include_directories(tag PRIVATE /opt/homebrew/include/libusb-1.0)
target_include_directories(tag PRIVATE  ${LIBUSB_1_INCLUDE_DIRS})

if (MSVC)
     target_link_libraries(tag PRIVATE ${libusb})
else()
     
     #string(REPLACE "dylib" "a" LIBUSB_1_LIBRARIES ${LIBUSB_1_LIBRARIES})
     message("libusb libraries: ${LIBUSB_1_LIBRARIES}")
     target_link_libraries(tag PRIVATE ${LIBUSB_1_LIBRARIES})
endif()

cmake_print_variables(LIBUSB_1_LIBRARIES)
cmake_print_variables(LIBUSB_LIBRARIES)
target_include_directories(tag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tag PUBLIC proto)
target_link_libraries(tag PRIVATE tag_monitor_interface)


