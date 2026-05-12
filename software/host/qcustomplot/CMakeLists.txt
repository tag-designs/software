cmake_minimum_required(VERSION 3.10)

project(qcustomplot LANGUAGES CXX)

message("Configure qcustomplot")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport CONFIG)

add_library(qcustomplot STATIC qcustomplot.cpp qcustomplot.h)
set_target_properties(qcustomplot PROPERTIES AUTOMOC ON)
target_include_directories(qcustomplot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)
target_compile_features(qcustomplot PUBLIC cxx_std_17)
