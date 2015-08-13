TARGET = composeplatforminputcontextplugin

PLUGIN_TYPE = platforminputcontexts
PLUGIN_EXTENDS = -
PLUGIN_CLASS_NAME = QComposePlatformInputContextPlugin
load(qt_plugin)

QT += gui-private

DEFINES += X11_PREFIX='\\"$$QMAKE_X11_PREFIX\\"'

SOURCES += $$PWD/qcomposeplatforminputcontextmain.cpp \
           $$PWD/qcomposeplatforminputcontext.cpp \
           $$PWD/generator/qtablegenerator.cpp \

HEADERS += $$PWD/qcomposeplatforminputcontext.h \
           $$PWD/generator/qtablegenerator.h \

# libxkbcommon
contains(QT_CONFIG, xkbcommon-qt): {
    # dont't need x11 dependency for compose key plugin
    QT_CONFIG -= use-xkbcommon-x11support
#    include(../../../3rdparty/xkbcommon.pri)
} else {
    LIBS += $$QMAKE_LIBS_XKBCOMMON
    QMAKE_CXXFLAGS += $$QMAKE_CFLAGS_XKBCOMMON
}

OTHER_FILES += $$PWD/compose.json
