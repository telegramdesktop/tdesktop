QT += core 

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Mac/DebugIntermediateEmoji
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Mac/DebugEmoji
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Mac/ReleaseIntermediateEmoji
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Mac/ReleaseEmoji
}

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/_other/Emoji.plist
    QMAKE_LFLAGS += -framework Cocoa
}

SOURCES += \
    ./SourceFiles/_other/memain.cpp \
    ./SourceFiles/_other/genemoji.cpp \

HEADERS += \
    ./SourceFiles/_other/memain.h \
    ./SourceFiles/_other/genemoji.h \

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.0/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.0/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\

