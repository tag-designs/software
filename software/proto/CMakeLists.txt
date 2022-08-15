cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message("Configure Proto")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS tag.proto tagdata.proto)

#
# Create the c++ protocol library for tags 
#

add_library(proto ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(proto PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(proto PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(proto PUBLIC protobuf::libprotobuf)

#
# Create a tool for translating json tag configuration files to 
# C code
#

add_executable(config-gen config-gen.cc)
target_include_directories(config-gen PRIVATE ${Protobuf_INCLUDE_DIRS})
target_link_libraries(config-gen PRIVATE proto)

#
# Create a library of tag protocol sources for 
# use in the embedded code
#

add_library(tag-proto-sources INTERFACE)
target_sources(tag-proto-sources INTERFACE 
                        ${CMAKE_CURRENT_LIST_DIR}/tag.proto 
                        ${CMAKE_CURRENT_LIST_DIR}/tagdata.proto)

