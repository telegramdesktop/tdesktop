T += core

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Mac/DebugIntermediateLang
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Mac/DebugLang
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Mac/ReleaseIntermediateLang
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Mac/ReleaseLang
}

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

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.0/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.0/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\

