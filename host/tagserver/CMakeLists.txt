# tag-server: the tagcore Tag API over gRPC, for test programs.
# Built only with -DBUILD_TAG_SERVER=ON; see README.md.

cmake_minimum_required(VERSION 3.28)

include(FetchContent)

# grpcxx is a small gRPC server library. It fetches and builds its own libuv,
# nghttp2 and fmt (GRPCXX_HERMETIC_BUILD), each pinned by hash in its
# cmake/dependencies.cmake.
#
# - nghttp2 declares BUILD_SHARED_LIBS as an option defaulting to ON, which
#   would make grpcxx, and any later target without an explicit type, a shared
#   library. Pin it OFF; the host build is static.
# - grpcxx asks for Protobuf 3.15.0, but protobuf's CMake package accepts only
#   requests whose major version is its C++ library's (6), so ask for 6.0.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(GRPCXX_HERMETIC_BUILD ON CACHE BOOL "Fetch grpcxx's dependencies" FORCE)
set(PROTOBUF_MINVERSION 6.0 CACHE STRING "Protobuf version grpcxx requires" FORCE)

FetchContent_Declare(grpcxx
  URL      https://github.com/uatuko/grpcxx/archive/refs/tags/v0.7.0.tar.gz
  URL_HASH SHA256=0bd8ab6e7a5df3206bc5c6fecd6c197014ffd08292ed9aa36fc252bf65db3db6
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(grpcxx)
if(MSVC)
  # nghttp2's API uses ssize_t, which MSVC lacks; NGHTTP2_NO_SSIZE_T selects
  # its ptrdiff_t form. grpcxx's own Windows CI builds with this; not yet
  # verified in this project's Windows build.
  target_compile_definitions(grpcxx PRIVATE NGHTTP2_NO_SSIZE_T)
endif()

# Messages and the grpcxx service header for tag_service.proto. tag.proto and
# tagdata.proto come from the `proto` library; google/rpc/status.proto is a
# vendored copy (Apache-2.0) for error details.
add_library(tag_service_proto STATIC
  ${PROJECT_SOURCE_DIR}/proto/tag_service.proto
  third_party/google/rpc/status.proto)
target_link_libraries(tag_service_proto PUBLIC proto protobuf::libprotobuf)
target_include_directories(tag_service_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
set(_tag_service_import_dirs
  ${PROJECT_SOURCE_DIR}/proto
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
protobuf_generate(TARGET tag_service_proto
  IMPORT_DIRS ${_tag_service_import_dirs}
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate(TARGET tag_service_proto
  LANGUAGE grpcxx
  GENERATE_EXTENSIONS .grpcxx.pb.h
  PLUGIN "protoc-gen-grpcxx=$<TARGET_FILE:libgrpcxx::protoc-gen>"
  IMPORT_DIRS ${_tag_service_import_dirs}
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

add_executable(tag-server main.cc tag_server.cc)
target_link_libraries(tag-server PRIVATE tag_service_proto tagcore libgrpcxx::grpcxx)
set_target_properties(tag-server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
