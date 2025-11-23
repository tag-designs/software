# Auto-generated deploy QML imports script for target "test3".
# Do not edit, all changes will be lost.
# This file should only be included by qt6_deploy_qml_imports().

set(__qt_opts BUNDLE)
if(arg_NO_QT_IMPORTS)
    list(APPEND __qt_opts NO_QT_IMPORTS)
endif()

_qt_internal_deploy_qml_imports_for_target(
    ${__qt_opts}
    IMPORTS_FILE "/Users/geobrown/tmp/qtquicktest/test3/build/Qt_6_8_3_for_macOS-Debug/.qt/qml_imports/test3_build.cmake"
    PLUGINS_FOUND __qt_internal_plugins_found
    QML_DIR     "test3.app/Contents/Resources/qml"
    PLUGINS_DIR "test3.app/Contents/PlugIns"
)

if(arg_PLUGINS_FOUND)
    set(${arg_PLUGINS_FOUND} "${__qt_internal_plugins_found}" PARENT_SCOPE)
endif()
