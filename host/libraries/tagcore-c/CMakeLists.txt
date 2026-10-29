#[[
  tagcore_c: a C interface to the C++ host libraries, built as one shared
  library for programs in other languages (the Rust tag-dwnld first).

  It exports only the tlw_* functions in taglogwriter_c.h, the tcs_*
  functions in calibsession_c.h and the svz_* functions in sensorviz_c.h.
  The C++ writer (tagcore), libprotobuf with abseil, and SQLite are linked
  statically inside it with every other symbol hidden, as are
  sensoranalysis's Qt-free calibration core behind tcs_* and sensorViz's
  Qt-free core behind svz_*, so a caller links one library and its own
  copies of those cannot clash. tagcore is linked whole as a static archive,
  so the writer's reference to Tag (SqliteTagLogWriter::writeHeader(Tag &))
  also brings in the link layer and libusb; they are never called.

  Built when BUILD_TAGCORE_C or BUILD_RUST_APPS is ON. Tag Monitor in Rust
  (host/applications/tagmonitor-rs) writes its downloads through it and installs
  it on Linux and Windows; macOS links tagcore_c_static into the program.
  See host/libraries/tagcore/design/proposals/rust-over-cpp-libraries.md.
#]]

add_library(tagcore_c SHARED
  calibsession_c.cc calibsession_c.h
  sensorviz_c.cc sensorviz_c.h
  taglogwriter_c.cc taglogwriter_c.h)

# A shared library can only take in position-independent code, and on Linux
# our own archives are not built that way by default (macOS always is, and
# Windows has no such rule). vcpkg's static libraries are built PIC already,
# so only our own need it. The release workflow's Linux build failed here
# ("relocation R_X86_64_PC32 ... recompile with -fPIC") until they were.
set_target_properties(tagcore proto sensoranalysis_core sensoranalysis_core_json
  sensordata sensorviz_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
target_compile_features(tagcore_c PRIVATE cxx_std_20)
target_compile_definitions(tagcore_c PRIVATE TAGCORE_C_BUILDING)
target_include_directories(tagcore_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tagcore_c PRIVATE tagcore sensoranalysis_core sensorviz_core)

# Hide everything but the tlw_* and tcs_* entry points. Windows DLLs export only what is
# marked dllexport, so they need no list.
set_target_properties(tagcore_c PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  # @rpath/libtagcore_c.dylib on macOS: the caller says where to look.
  INSTALL_NAME_DIR "@rpath"
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/lib
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
if(APPLE)
  target_link_options(tagcore_c PRIVATE
    "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.macos"
    "LINKER:-dead_strip")
  set_property(TARGET tagcore_c APPEND PROPERTY LINK_DEPENDS
               ${CMAKE_CURRENT_SOURCE_DIR}/exports.macos)
elseif(UNIX)
  target_link_options(tagcore_c PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.linux"
    "LINKER:--gc-sections"
    "LINKER:--exclude-libs,ALL")
  set_property(TARGET tagcore_c APPEND PROPERTY LINK_DEPENDS
               ${CMAKE_CURRENT_SOURCE_DIR}/exports.linux)
endif()

# The same interface as a static archive (target tagcore_c_static).
add_subdirectory(static)
