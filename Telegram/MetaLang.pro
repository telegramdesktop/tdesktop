T += core

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Linux/DebugIntermediateLang
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Linux/DebugLang
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Linux/ReleaseIntermediateLang
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Linux/ReleaseLang
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

