QT += core 

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../DebugIntermediateStyle
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../DebugStyle
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../ReleaseIntermediateStyle
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../ReleaseStyle
}

CONFIG += plugin static

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

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.1/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.1/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\

