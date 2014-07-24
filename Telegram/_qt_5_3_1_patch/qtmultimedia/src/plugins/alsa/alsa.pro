TARGET = qtaudio_alsa
QT += multimedia-private

PLUGIN_TYPE = audio
PLUGIN_CLASS_NAME = QAlsaPlugin
load(qt_plugin)

LIBS += -lasound
CONFIG += static plugin
HEADERS += \
    qalsaplugin.h \
    qalsaaudiodeviceinfo.h \
    qalsaaudioinput.h \
    qalsaaudiooutput.h

SOURCES += \
    qalsaplugin.cpp \
    qalsaaudiodeviceinfo.cpp \
    qalsaaudioinput.cpp \
    qalsaaudiooutput.cpp

OTHER_FILES += \
    alsa.json
