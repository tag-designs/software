set(HOST_MKDOCS_COMMAND [==[/opt/homebrew/bin/mkdocs]==])
set(HOST_DOCS_CONFIG [==[/Users/geobrown/Research/tag-designs/software/host/docs/mkdocs.yml]==])
set(HOST_DOCS_BUILD_DIR [==[/Users/geobrown/Research/tag-designs/software/build-embedded/host/docs/site]==])
set(HOST_DOCS_SOURCE_DIR [==[/Users/geobrown/Research/tag-designs/software/host/docs]==])

message(STATUS "Building host application user guide")
execute_process(
  COMMAND ${HOST_MKDOCS_COMMAND} build
    --config-file "${HOST_DOCS_CONFIG}"
    --site-dir "${HOST_DOCS_BUILD_DIR}"
    --clean
  WORKING_DIRECTORY "${HOST_DOCS_SOURCE_DIR}"
  RESULT_VARIABLE host_docs_result)
if(NOT host_docs_result EQUAL 0)
  message(FATAL_ERROR "Failed to build host application user guide")
endif()
