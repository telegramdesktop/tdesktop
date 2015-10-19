QT += core 

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../DebugIntermediatePacker
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Debug
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../ReleaseIntermediatePacker
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Release
}

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/_other/Packer.plist
    QMAKE_LFLAGS += -framework Cocoa
}

SOURCES += \
    ./SourceFiles/_other/packer.cpp \

HEADERS += \
    ./SourceFiles/_other/packer.h \

unix {
    linux-g++:QMAKE_TARGET.arch = $$QMAKE_HOST.arch
    linux-g++-32:QMAKE_TARGET.arch = x86
    linux-g++-64:QMAKE_TARGET.arch = x86_64

    contains(QMAKE_TARGET.arch, x86_64) {
        DEFINES += Q_OS_LINUX64
    } else {
        DEFINES += Q_OS_LINUX32
    }
}

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.5.1/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.5.1/QtCore\
               ./../../Libraries/QtStatic/qtbase/include

LIBS += -lcrypto -lssl -lz -llzma
