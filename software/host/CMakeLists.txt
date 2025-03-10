cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 20)

# set(CMAKE_BUILD_TYPE Debug)

if (apple)
set(Qt6_DIR "/Users/geobrown/Qt/6.8.2s/macos")
endif()

if (WIN32)
set(Qt6_DIR "d:/Qt/x64-static")
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)

add_subdirectory(lib)
add_subdirectory(cli)
add_subdirectory(qtmon)
add_subdirectory(qtprogram)
add_subdirectory(btdataviz)

if(WIN32)
  install(TARGETS btviz qtmonitor ptprogram 
          BUNDLE DESTINATION .
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  install(TARGETS btviz qtmonitor qtprogram 
          BUNDLE DESTINATION UltralightTags
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

