QT += core

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../DebugIntermediateLang
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../DebugLang
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../ReleaseIntermediateLang
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../ReleaseLang
}

CONFIG += plugin static

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/_other/Lang.plist
    QMAKE_LFLAGS += -framework Cocoa
}

SOURCES += \
    ./SourceFiles/_other/mlmain.cpp \
    ./SourceFiles/_other/genlang.cpp \

HEADERS += \
    ./SourceFiles/_other/mlmain.h \
    ./SourceFiles/_other/genlang.h \

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.1/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.1/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\

