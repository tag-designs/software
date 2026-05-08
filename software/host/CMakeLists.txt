cmake_minimum_required(VERSION 3.15)

# set(CMAKE_BUILD_TYPE Debug)

if (APPLE)
#set(Qt6_DIR "$ENV{HOME}/Qt/6.8.2/macos/lib/cmake/Qt6")
endif()

if (WIN32)
set(Qt6_DIR "c:/Qt6_static/lib/cmake/Qt6")
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
set(HOST_BUNDLE_INSTALL_DIR UltralightTags)

add_subdirectory(lib)
add_subdirectory(cli)
if(BUILD_QT_APPS)
  add_subdirectory(qtmon)
  add_subdirectory(qtprogram)
  add_subdirectory(qtcalibrate)
  add_subdirectory(btdataviz)
  add_subdirectory(compviz)
  add_subdirectory(tag-orientation)
endif()

set(host_install_targets)
foreach(target_name btviz compviz qtmonitor qtprogram qtcalibrate)
  if(TARGET ${target_name})
    list(APPEND host_install_targets ${target_name})
  endif()
endforeach()

if(host_install_targets)
  if(APPLE)
    foreach(target_name ${host_install_targets})
      set_property(TARGET ${target_name} APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
    endforeach()
  endif()

  install(
    TARGETS ${host_install_targets}
    BUNDLE DESTINATION ${HOST_BUNDLE_INSTALL_DIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  if(WIN32 OR APPLE)
    include(DeployQt)
    foreach(target_name ${host_install_targets})
      if(WIN32)
        install_windeployqt(${target_name})
      elseif(APPLE)
        install_macdeployqt(${target_name})
        if(NOT MACOS_USE_MACDEPLOYQT_DEPLOYMENT)
          install_macos_qt_plugins(${target_name})
          install_macos_prune_qt_sql_drivers(${target_name})
          install_macos_remove_bundle_signatures(${target_name})
          install_macos_fixup_bundle(${target_name})
          install_macos_codesign(${target_name})
        endif()
      endif()
    endforeach()
  endif()
endif()
