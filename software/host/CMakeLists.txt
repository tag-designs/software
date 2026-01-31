cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 20)

# set(CMAKE_BUILD_TYPE Debug)

if (apple)
#set(Qt6_DIR "$ENV{HOME}/Qt/6.8.2/macos/lib/cmake/Qt6")
endif()

if (WIN32)
#set(Qt6_DIR "c:Qt/6.10.2/msvc2022_64/lib/cmake/Qt6")
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

add_subdirectory(lib)
add_subdirectory(cli)
add_subdirectory(qtmon)
add_subdirectory(qtprogram)
add_subdirectory(qtcalibrate)
add_subdirectory(btdataviz)
add_subdirectory(compviz)
add_subdirectory(tag-orientation)

if(WIN32)
  #add_dependencies(qtmonitor BitTag PresTag)
  install(TARGETS btviz compviz qtmonitor qtprogram qtcalibrate
          BUNDLE DESTINATION UltralightTags
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  install(TARGETS btviz compviz qtmonitor qtprogram qtcalibrate
          BUNDLE DESTINATION UltralightTags
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

