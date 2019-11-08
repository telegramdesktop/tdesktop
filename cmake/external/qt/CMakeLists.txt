add_library(external_qt INTERFACE IMPORTED GLOBAL)

target_include_directories(external_qt SYSTEM
INTERFACE
    ${qt_loc}/include
    ${qt_loc}/include/QtCore
    ${qt_loc}/include/QtGui
    ${qt_loc}/include/QtDBus
    ${qt_loc}/include/QtCore/${qt_version}
    ${qt_loc}/include/QtGui/${qt_version}
    ${qt_loc}/include/QtCore/${qt_version}/QtCore
    ${qt_loc}/include/QtGui/${qt_version}/QtGui
)

set(common_qt_libs
    qwebp
    qgif
    qjpeg
    Qt5PrintSupport
    Qt5AccessibilitySupport
    Qt5FontDatabaseSupport
    Qt5EventDispatcherSupport
    Qt5ThemeSupport
    Qt5Network
    Qt5Widgets
    Qt5Gui
    qtharfbuzz
    qtlibpng
)

if (WIN32)
    set(qt_libs
        ${common_qt_libs}
        Qt5Core
        Qt5WindowsUIAutomationSupport
        qtmain
        qwindows
        qtfreetype
        qtpcre2
    )
    set(qt_libs_list "")
    foreach(lib ${qt_libs})
        list(APPEND qt_libs_list "${lib}$<$<CONFIG:Debug>:d>")
    endforeach()
else()
endif()

target_link_directories(external_qt
INTERFACE
    ${qt_loc}/lib
    ${qt_loc}/plugins
    ${qt_loc}/plugins/bearer
    ${qt_loc}/plugins/platforms
    ${qt_loc}/plugins/imageformats
)

target_link_libraries(external_qt
INTERFACE
    ${qt_libs_list}
)
