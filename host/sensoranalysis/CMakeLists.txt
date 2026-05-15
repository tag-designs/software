cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

message("Configure sensoranalysis")

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(sensoranalysis STATIC
  compass_processor.cpp
  compass_types.cpp)

target_include_directories(sensoranalysis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sensoranalysis PUBLIC Qt6::Core Qt6::Gui)
