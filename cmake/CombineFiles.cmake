if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT is required")
endif()

if(NOT DEFINED INPUTS)
  message(FATAL_ERROR "INPUTS is required")
endif()

file(WRITE "${OUTPUT}" "")

set(disabled_messages)
set(enabled_messages)

foreach(input IN LISTS INPUTS)
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "Input file does not exist: ${input}")
  endif()

  file(READ "${input}" input_content)
  string(REPLACE "\r\n" "\n" input_content "${input_content}")
  string(REPLACE "\n" ";" input_lines "${input_content}")

  foreach(line IN LISTS input_lines)
    string(STRIP "${line}" stripped_line)
    if(stripped_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[ \t]+skip_message:true")
      list(APPEND disabled_messages "${CMAKE_MATCH_1}")
    elseif(stripped_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[ \t]+skip_message:false")
      list(APPEND enabled_messages "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endforeach()

foreach(input IN LISTS INPUTS)
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "Input file does not exist: ${input}")
  endif()

  get_filename_component(input_name "${input}" NAME)
  file(APPEND "${OUTPUT}" "# ${input_name}\n")
  file(READ "${input}" input_content)
  string(REPLACE "\r\n" "\n" input_content "${input_content}")
  string(REPLACE "\n" ";" input_lines "${input_content}")

  foreach(line IN LISTS input_lines)
    string(STRIP "${line}" stripped_line)
    set(skip_line FALSE)

    if(stripped_line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)\\.[^ \t]+[ \t]+.*max_(size|count):")
      set(message_name "${CMAKE_MATCH_1}")
      list(FIND disabled_messages "${message_name}" disabled_index)
      list(FIND enabled_messages "${message_name}" enabled_index)
      if(disabled_index GREATER -1 AND enabled_index EQUAL -1)
        set(skip_line TRUE)
      endif()
    endif()

    if(NOT skip_line)
      file(APPEND "${OUTPUT}" "${line}\n")
    endif()
  endforeach()

  file(APPEND "${OUTPUT}" "\n")
endforeach()
