cmake_minimum_required(VERSION 3.15)

message("Configure Tag Library")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB_1_0 REQUIRED IMPORTED_TARGET libusb-1.0)

#
# Create tag library 
#

add_library(tag STATIC)
target_link_libraries(tag PRIVATE PkgConfig::LIBUSB_1_0)
target_compile_features(tag PUBLIC cxx_std_20)

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

target_include_directories(tag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tag PUBLIC proto)
target_link_libraries(tag PRIVATE tag_monitor_interface)

