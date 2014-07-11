TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += ./SourceFiles/_other/updater_linux.cpp

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../DebugIntermediateUpdater
    DESTDIR = ./../Debug
}
CONFIG(release, debug|release) {
    DEFINES += CUSTOM_API_ID
    OBJECTS_DIR = ./../ReleaseIntermediateUpdater
    DESTDIR = ./../Release
}
