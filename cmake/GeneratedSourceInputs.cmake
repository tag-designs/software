# Which files a generated/ directory's manifest hashes.
#
# This exists because the list was written out twice -- once in
# add_nanopb_target, which writes the manifests, and once in
# CheckGeneratedSourcesFresh.cmake, which recomputes them -- and nothing made
# the two agree. They disagreed the moment config-gen moved from C++ to Python:
# the build hashed config-gen.py, the check still hashed config-gen.cc, and
# every committed inputs.sha256 would have looked stale on every pull request.
#
# The same reasoning already applies elsewhere in this machinery. InputManifest
# produces the manifest text for both writer and checker so they cannot disagree
# about the format; the check reads the .proto list out of proto/CMakeLists.txt
# rather than repeating it; each board's PROCESSOR is read from its own
# CMakeLists.txt. This closes the one remaining place where a list was kept in
# two hands.
#
# Order does not matter: input_manifest_text sorts before hashing.

# The inputs behind one proto-c variant's generated sources.
#
#   SOURCE_DIR    the repository root
#   VARIANT_DIR   the variant's source directory, e.g. embedded/proto-c/bittag-proto-c
#   PROTO_SOURCES absolute paths of the .proto files in tag-proto-sources
function(proto_c_manifest_inputs out_var)
  cmake_parse_arguments(PI "" "SOURCE_DIR;VARIANT_DIR" "PROTO_SOURCES" ${ARGN})
  foreach(_required SOURCE_DIR VARIANT_DIR PROTO_SOURCES)
    if(NOT PI_${_required})
      message(FATAL_ERROR "proto_c_manifest_inputs: ${_required} is required")
    endif()
  endforeach()

  set(_proto_c_dir "${PI_SOURCE_DIR}/embedded/proto-c")
  set(_inputs
      "${PI_VARIANT_DIR}/default-config.json"
      "${_proto_c_dir}/config-gen.py"
      "${PI_SOURCE_DIR}/cmake/CombineFiles.cmake")
  list(APPEND _inputs ${PI_PROTO_SOURCES})

  foreach(_proto IN LISTS PI_PROTO_SOURCES)
    get_filename_component(_stem "${_proto}" NAME_WE)
    list(APPEND _inputs
         "${_proto_c_dir}/default-options/${_stem}.options"
         "${PI_VARIANT_DIR}/${_stem}.override.options")
  endforeach()

  set(${out_var} "${_inputs}" PARENT_SCOPE)
endfunction()
