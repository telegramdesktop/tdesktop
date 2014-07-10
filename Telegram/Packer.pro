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

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.1/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.1/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\
               ./../../Libraries/lzma/C\
               /usr/local/ssl/include

LIBS += -L/usr/local/ssl/lib -lcrypto -lssl -lz -llzma
