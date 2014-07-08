QT += core 

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Linux/DebugIntermediateStyle
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Linux/DebugStyle
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Linux/ReleaseIntermediateStyle
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Linux/ReleaseStyle
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

