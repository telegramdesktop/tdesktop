QT += core 

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Mac/DebugIntermediateStyle
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Mac/DebugStyle
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Mac/ReleaseIntermediateStyle
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Mac/ReleaseStyle
}

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/_other/Style.plist
    QMAKE_LFLAGS += -framework Cocoa
}

SOURCES += \
    ./SourceFiles/_other/msmain.cpp \
    ./SourceFiles/_other/genstyles.cpp \

HEADERS += \
    ./SourceFiles/_other/msmain.h \
    ./SourceFiles/_other/genstyles.h \

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.0/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.0/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\

