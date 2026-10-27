#[[
  tagcore_c_static: the same C interface as tagcore_c, as one static archive
  (libtagcore_c.a) for a caller that wants a self-contained executable.

  The shim, tagcore, libprotobuf with abseil and SQLite are pre-linked into a
  single relocatable object with the export list applied, so only the tlw_*
  functions stay global, exactly as in the shared library; then the object is
  wrapped as an archive. The caller links that one archive plus the C++
  runtime and the system libraries the pre-linked code calls.

  The pre-link reuses CMake's own link line, which knows every archive the
  writer pulls in: this directory redefines the MODULE link rule as
  `c++ -r`, so the rule applies to this target only.

  macOS only, by choice: each macOS application is its own bundle and would
  carry its own copy of a shared library anyway. Linux and Windows link the
  shared tagcore_c, one copy of which serves every tool in the install
  directory.
#]]

if(NOT APPLE)
  message(STATUS "tagcore_c_static: macOS only; Linux and Windows link tagcore_c")
  return()
endif()

set(CMAKE_CXX_CREATE_SHARED_MODULE
    "<CMAKE_CXX_COMPILER> <LANGUAGE_COMPILE_FLAGS> -r -nostdlib <LINK_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")

add_library(tagcore_c_prelink MODULE ../taglogwriter_c.cc)
target_compile_features(tagcore_c_prelink PRIVATE cxx_std_20)
target_compile_definitions(tagcore_c_prelink PRIVATE TAGCORE_C_BUILDING)
target_include_directories(tagcore_c_prelink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tagcore_c_prelink PRIVATE tagcore)
target_link_options(tagcore_c_prelink PRIVATE
  "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/../exports.macos")
set_target_properties(tagcore_c_prelink PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX ""
  SUFFIX ".o"
  OUTPUT_NAME tagcore_c_prelinked
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../exports.macos)

set(_archive "${CMAKE_BINARY_DIR}/$<CONFIG>/lib/libtagcore_c.a")
add_custom_target(tagcore_c_static
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>/lib"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${_archive}"
  COMMAND xcrun libtool -static -o "${_archive}" "$<TARGET_FILE:tagcore_c_prelink>"
  DEPENDS tagcore_c_prelink
  BYPRODUCTS "${_archive}"
  COMMENT "Archiving the pre-linked tagcore_c as libtagcore_c.a"
  VERBATIM)
