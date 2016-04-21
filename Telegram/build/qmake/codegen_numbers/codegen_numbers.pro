QT += core gui

TARGET = codegen_numbers
CONFIG += console static c++14
CONFIG -= app_bundle

CONFIG(debug, debug|release) {
    OBJECTS_DIR = ./
    DESTDIR = ./../../../codegen/Debug
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./
    DESTDIR = ./../../../codegen/Release
}

INCLUDEPATH += ./../../../SourceFiles

QMAKE_CFLAGS_WARN_ON += -Wno-missing-field-initializers
QMAKE_CXXFLAGS_WARN_ON += -Wno-missing-field-initializers

TEMPLATE = app

SOURCES += \
./../../../SourceFiles/codegen/common/basic_tokenized_file.cpp \
./../../../SourceFiles/codegen/common/checked_utf8_string.cpp \
./../../../SourceFiles/codegen/common/clean_file.cpp \
./../../../SourceFiles/codegen/common/cpp_file.cpp \
./../../../SourceFiles/codegen/common/logging.cpp \
./../../../SourceFiles/codegen/numbers/generator.cpp \
./../../../SourceFiles/codegen/numbers/main.cpp \
./../../../SourceFiles/codegen/numbers/options.cpp \
./../../../SourceFiles/codegen/numbers/parsed_file.cpp \
./../../../SourceFiles/codegen/numbers/processor.cpp

HEADERS += \
./../../../SourceFiles/codegen/common/basic_tokenized_file.h \
./../../../SourceFiles/codegen/common/checked_utf8_string.h \
./../../../SourceFiles/codegen/common/clean_file.h \
./../../../SourceFiles/codegen/common/clean_file_reader.h \
./../../../SourceFiles/codegen/common/cpp_file.h \
./../../../SourceFiles/codegen/common/logging.h \
./../../../SourceFiles/codegen/numbers/generator.h \
./../../../SourceFiles/codegen/numbers/options.h \
./../../../SourceFiles/codegen/numbers/parsed_file.h \
./../../../SourceFiles/codegen/numbers/processor.h
