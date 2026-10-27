#[[
  qtmon_core: qtmonitor's Qt-free core - the rules for which commands are
  available, the start checks, small tag rules, and (as the restructure
  proceeds) the monitor model, the configuration document and the download
  loop. The window links it; so can a check program or a port, with no Qt.

  Needs only the generated protobuf library (proto) and tagcore. See
  ../design/architecture.md.
#]]

add_library(qtmon_core STATIC
  commands.cc
  configdocument.cc
  startchecks.cc
  tagrules.cc)
target_include_directories(qtmon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(qtmon_core PUBLIC cxx_std_20)
target_link_libraries(qtmon_core PUBLIC proto tagcore)
# Plain C++: keep the parent directory's AUTOMOC and AUTOUIC off this target.
set_target_properties(qtmon_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
