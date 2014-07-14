TARGET = qtmedia_pulse
QT += multimedia-private

PLUGIN_TYPE = audio
PLUGIN_CLASS_NAME = QPulseAudioPlugin
load(qt_plugin)

CONFIG += link_pkgconfig
PKGCONFIG += libpulse
CONFIG += static plugin
HEADERS += qpulseaudioplugin.h \
           qaudiodeviceinfo_pulse.h \
           qaudiooutput_pulse.h \
           qaudioinput_pulse.h \
           qpulseaudioengine.h \
           qpulsehelpers.h

SOURCES += qpulseaudioplugin.cpp \
           qaudiodeviceinfo_pulse.cpp \
           qaudiooutput_pulse.cpp \
           qaudioinput_pulse.cpp \
           qpulseaudioengine.cpp \
           qpulsehelpers.cpp

OTHER_FILES += \
    pulseaudio.json
