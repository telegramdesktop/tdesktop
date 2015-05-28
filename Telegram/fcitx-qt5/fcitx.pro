TARGET = fcitxplatforminputcontextplugin

PLUGIN_TYPE = platforminputcontexts
PLUGIN_EXTENDS = -
PLUGIN_CLASS_NAME = QFcitxPlatformInputContextPlugin
load(qt_plugin)

QT += dbus widgets gui-private
SOURCES += $$PWD/fcitxqtconfiguifactory.cpp \
		   $$PWD/fcitxqtconfiguiplugin.cpp \
		   $$PWD/fcitxqtconfiguiwidget.cpp \
		   $$PWD/fcitxqtconnection.cpp \
		   $$PWD/fcitxqtformattedpreedit.cpp \
		   $$PWD/fcitxqtinputcontextproxy.cpp \
		   $$PWD/fcitxqtinputmethoditem.cpp \
		   $$PWD/fcitxqtinputmethodproxy.cpp \
		   $$PWD/fcitxqtkeyboardlayout.cpp \
		   $$PWD/fcitxqtkeyboardproxy.cpp \
		   $$PWD/fcitxqtkeysequencewidget.cpp \
		   $$PWD/keyuni.cpp \
		   $$PWD/main.cpp \
		   $$PWD/qfcitxplatforminputcontext.cpp \
		   $$PWD/qtkeytrans.cpp \
		   $$PWD/utils.cpp

HEADERS += $$PWD/fcitxqtconfiguifactory.h \
		   $$PWD/fcitxqtconfiguifactory_p.h \
		   $$PWD/fcitxqtconfiguiplugin.h \
		   $$PWD/fcitxqtconfiguiwidget.h \
		   $$PWD/fcitxqtconnection.h \
		   $$PWD/fcitxqtconnection_p.h \
		   $$PWD/fcitxqtformattedpreedit.h \
		   $$PWD/fcitxqtinputcontextproxy.h \
		   $$PWD/fcitxqtinputmethoditem.h \
		   $$PWD/fcitxqtinputmethodproxy.h \
		   $$PWD/fcitxqtkeyboardlayout.h \
		   $$PWD/fcitxqtkeyboardproxy.h \
		   $$PWD/fcitxqtkeysequencewidget.h \
		   $$PWD/fcitxqtkeysequencewidget_p.h \
		   $$PWD/keydata.h \
		   $$PWD/keyserver_x11.h \
		   $$PWD/keyuni.h \
		   $$PWD/main.h \
		   $$PWD/qfcitxplatforminputcontext.h \
		   $$PWD/qtkeytransdata.h \
		   $$PWD/qtkeytrans.h \
		   $$PWD/utils.h
OTHER_FILES += $$PWD/fcitx.json
