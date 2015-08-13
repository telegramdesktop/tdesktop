TARGET = fcitxplatforminputcontextplugin

PLUGIN_TYPE = platforminputcontexts
PLUGIN_EXTENDS = -
PLUGIN_CLASS_NAME = QFcitxPlatformInputContextPlugin
load(qt_plugin)

QT += dbus gui-private
SOURCES +=	$$PWD/fcitxqtconnection.cpp \
			$$PWD/fcitxqtformattedpreedit.cpp \
			$$PWD/fcitxqtinputcontextproxy.cpp \
			$$PWD/fcitxqtinputmethoditem.cpp \
			$$PWD/fcitxqtinputmethodproxy.cpp \
			$$PWD/fcitxqtkeyboardlayout.cpp \
			$$PWD/fcitxqtkeyboardproxy.cpp \
			$$PWD/keyuni.cpp \
			$$PWD/main.cpp \
			$$PWD/qfcitxplatforminputcontext.cpp \
			$$PWD/utils.cpp

HEADERS +=	$$PWD/fcitxqtconnection.h \
			$$PWD/fcitxqtconnection_p.h \
			$$PWD/fcitxqtdbusaddons_export.h \
			$$PWD/fcitxqtdbusaddons_version.h \
			$$PWD/fcitxqtformattedpreedit.h \
			$$PWD/fcitxqtinputcontextproxy.h \
			$$PWD/fcitxqtinputmethoditem.h \
			$$PWD/fcitxqtinputmethodproxy.h \
			$$PWD/fcitxqtkeyboardlayout.h \
			$$PWD/fcitxqtkeyboardproxy.h \
			$$PWD/keydata.h \
			$$PWD/keyserver_x11.h \
			$$PWD/keyuni.h \
			$$PWD/main.h \
			$$PWD/qfcitxplatforminputcontext.h \
			$$PWD/utils.h

OTHER_FILES += $$PWD/fcitx.jsn
