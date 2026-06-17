cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "GenerateConfigVisibility requires OUTPUT")
endif()

if(NOT DEFINED INPUTS)
  message(FATAL_ERROR "GenerateConfigVisibility requires INPUTS")
endif()

set(CONFIG_VISIBILITY_FIELDS
    active_interval
    hibernate
    period
    start_delay
    bittag_log
    adxl362
    adxl362_range
    adxl362_freq
    adxl362_filter
    adxl362_act_thresh_g
    adxl362_inact_thresh_g
    adxl362_inactive_sec
    adxl362_accel_type
    adxl362_data_format
    adxl362_channels
    adxl362_active_sec
    lsm6
    lsm6_odr
    lsm6_accel_rng
    lsm6_gyro_rng)

function(text_has_key out text)
  set(found false)
  foreach(key IN LISTS ARGN)
    if("${text}" MATCHES "\"${key}\"[ \t\r\n]*:")
      set(found true)
    endif()
  endforeach()
  set(${out} ${found} PARENT_SCOPE)
endfunction()

function(or_field tag_type field value)
  set(var "visibility_${tag_type}_${field}")
  if(${value})
    set(${var} true PARENT_SCOPE)
  elseif(NOT DEFINED ${var})
    set(${var} false PARENT_SCOPE)
  else()
    set(${var} "${${var}}" PARENT_SCOPE)
  endif()
endfunction()

function(emit_bool out name value)
  if(${value})
    set(${out} "    .${name} = true,\n" PARENT_SCOPE)
  else()
    set(${out} "    .${name} = false,\n" PARENT_SCOPE)
  endif()
endfunction()

foreach(input IN LISTS INPUTS)
  file(READ "${input}" json_text)

  if("${json_text}" MATCHES "\"tag_type\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"")
    set(tag_type "${CMAKE_MATCH_1}")
  else()
    message(FATAL_ERROR "${input} does not define required field tag_type")
  endif()

  list(FIND tag_types "${tag_type}" tag_index)
  if(tag_index EQUAL -1)
    list(APPEND tag_types "${tag_type}")
  endif()

  text_has_key(has_active_interval "${json_text}" active_interval activeInterval)
  text_has_key(has_hibernate "${json_text}" hibernate)
  text_has_key(has_period "${json_text}" period)
  text_has_key(has_start_delay "${json_text}" start_delay startDelay)
  text_has_key(has_bittag_log "${json_text}" bittag_log bittagLog)

  text_has_key(has_adxl362 "${json_text}" adxl362)
  text_has_key(has_adxl362_range "${json_text}" range)
  text_has_key(has_adxl362_freq "${json_text}" freq)
  text_has_key(has_adxl362_filter "${json_text}" filter)
  text_has_key(has_adxl362_act_thresh_g "${json_text}" act_thresh_g actThreshG)
  text_has_key(has_adxl362_inact_thresh_g "${json_text}" inact_thresh_g inactThreshG)
  text_has_key(has_adxl362_inactive_sec "${json_text}" inactive_sec inactiveSec)
  text_has_key(has_adxl362_accel_type "${json_text}" accel_type accelType)
  text_has_key(has_adxl362_data_format "${json_text}" data_format dataFormat)
  text_has_key(has_adxl362_channels "${json_text}" channels)
  text_has_key(has_adxl362_active_sec "${json_text}" active_sec activeSec)

  text_has_key(has_lsm6 "${json_text}" lsm6)
  text_has_key(has_lsm6_odr "${json_text}" odr)
  text_has_key(has_lsm6_accel_rng "${json_text}" accel_rng accelRng)
  text_has_key(has_lsm6_gyro_rng "${json_text}" gyro_rng gyroRng)

  foreach(field IN LISTS CONFIG_VISIBILITY_FIELDS)
    or_field("${tag_type}" "${field}" "has_${field}")
  endforeach()
endforeach()

set(table_entries "")
set(switch_entries "")

foreach(tag_type IN LISTS tag_types)
  string(TOLOWER "${tag_type}" tag_lower)
  set(var_name "visibility_${tag_lower}")

  set(entry "  static const ConfigFieldVisibility ${var_name} = {\n")
  foreach(field IN LISTS CONFIG_VISIBILITY_FIELDS)
    set(field_var "visibility_${tag_type}_${field}")
    emit_bool(line "${field}" "${field_var}")
    string(APPEND entry "${line}")
  endforeach()
  string(APPEND entry "  };\n\n")
  string(APPEND table_entries "${entry}")
  string(APPEND switch_entries "    case ${tag_type}:\n      return ${var_name};\n")
endforeach()

string(CONCAT output_text
  "#include \"configfieldvisibility.h\"\n"
  "\n"
  "const ConfigFieldVisibility &configFieldVisibilityForTag(TagType tag_type)\n"
  "{\n"
  "  static const ConfigFieldVisibility empty = {};\n"
  "${table_entries}"
  "  switch (tag_type)\n"
  "  {\n"
  "${switch_entries}"
  "    default:\n"
  "      return empty;\n"
  "  }\n"
  "}\n")

file(WRITE "${OUTPUT}" "${output_text}")
