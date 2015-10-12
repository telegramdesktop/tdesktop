QT += core gui network widgets

CONFIG += plugin static

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../DebugIntermediate
    MOC_DIR = ./GenFiles/Debug
    RCC_DIR = ./GenFiles
    DESTDIR = ./../Debug
}
CONFIG(release, debug|release) {
    DEFINES += _WITH_DEBUG CUSTOM_API_ID
    OBJECTS_DIR = ./../ReleaseIntermediate
    MOC_DIR = ./GenFiles/Release
    RCC_DIR = ./GenFiles
    DESTDIR = ./../Release
}

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/Telegram.plist
    OBJECTIVE_SOURCES += ./SourceFiles/pspecific_mac_p.mm
    OBJECTIVE_HEADERS += ./SourceFiles/pspecific_mac_p.h
    QMAKE_LFLAGS += -framework Cocoa
}

linux {
    SOURCES += ./SourceFiles/pspecific_linux.cpp
    HEADERS += ./SourceFiles/pspecific_linux.h
}

style_auto_cpp.target = ./GeneratedFiles/style_auto.cpp
style_auto_cpp.depends = FORCE
style_auto_cpp.commands = mkdir -p ./../../Telegram/GeneratedFiles && ./../DebugStyle/MetaStyle -classes_in ./../../Telegram/Resources/style_classes.txt -classes_out ./../../Telegram/GeneratedFiles/style_classes.h -styles_in ./../../Telegram/Resources/style.txt -styles_out ./../../Telegram/GeneratedFiles/style_auto.h -path_to_sprites ./../../Telegram/SourceFiles/art/
style_auto_cpp.depends = ./../../Telegram/Resources/style.txt

style_auto_h.target = ./GeneratedFiles/style_auto.h
style_auto_h.depends = FORCE
style_auto_h.commands = mkdir -p ./../../Telegram/GeneratedFiles && ./../DebugStyle/MetaStyle -classes_in ./../../Telegram/Resources/style_classes.txt -classes_out ./../../Telegram/GeneratedFiles/style_classes.h -styles_in ./../../Telegram/Resources/style.txt -styles_out ./../../Telegram/GeneratedFiles/style_auto.h -path_to_sprites ./../../Telegram/SourceFiles/art/
style_auto_h.depends = ./../../Telegram/Resources/style.txt

style_classes_h.target = ./GeneratedFiles/style_classes.h
style_classes_h.depends = FORCE
style_classes_h.commands = mkdir -p ./../../Telegram/GeneratedFiles && ./../DebugStyle/MetaStyle -classes_in ./../../Telegram/Resources/style_classes.txt -classes_out ./../../Telegram/GeneratedFiles/style_classes.h -styles_in ./../../Telegram/Resources/style.txt -styles_out ./../../Telegram/GeneratedFiles/style_auto.h -path_to_sprites ./../../Telegram/SourceFiles/art/
style_classes_h.depends = ./../../Telegram/Resources/style_classes.txt

numbers_cpp.target = ./GeneratedFiles/numbers.cpp
numbers_cpp.depends = FORCE
numbers_cpp.commands = mkdir -p ./../../Telegram/GeneratedFiles && ./../DebugStyle/MetaStyle -classes_in ./../../Telegram/Resources/style_classes.txt -classes_out ./../../Telegram/GeneratedFiles/style_classes.h -styles_in ./../../Telegram/Resources/style.txt -styles_out ./../../Telegram/GeneratedFiles/style_auto.h -path_to_sprites ./../../Telegram/SourceFiles/art/
numbers_cpp.depends = ./../../Telegram/Resources/numbers.txt 

lang_auto_cpp.target = ./GeneratedFiles/lang_auto.cpp
lang_auto_cpp.depends = FORCE
lang_auto_cpp.commands = mkdir -p ./../../Telegram/GeneratedFiles && ./../DebugLang/MetaLang -lang_in ./../../Telegram/Resources/lang.strings -lang_out ./../../Telegram/GeneratedFiles/lang_auto
lang_auto_cpp.depends = ./../../Telegram/Resources/lang.strings

lang_auto_h.target = ./GeneratedFiles/lang_auto.h
lang_auto_h.depends = FORCE
lang_auto_h.commands = mkdir -p ./../../Telegram/GeneratedFiles && ./../DebugLang/MetaLang -lang_in ./../../Telegram/Resources/lang.strings -lang_out ./../../Telegram/GeneratedFiles/lang_auto
lang_auto_h.depends = ./../../Telegram/Resources/lang.strings

hook.depends = style_auto_cpp style_auto_h style_classes_h numbers_cpp lang_auto_cpp lang_auto_h
CONFIG(debug,debug|release):hook.target = Makefile.Debug
CONFIG(release,debug|release):hook.target = Makefile.Release

QMAKE_EXTRA_TARGETS += style_auto_cpp style_auto_h style_classes_h numbers_cpp lang_auto_cpp lang_auto_h hook

PRE_TARGETDEPS += ./GeneratedFiles/style_auto.cpp ./GeneratedFiles/style_auto.h ./GeneratedFiles/style_classes.h ./GeneratedFiles/numbers.cpp ./GeneratedFiles/lang_auto.h ./GeneratedFiles/lang_auto.cpp

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

SOURCES += \
    ./SourceFiles/main.cpp \
    ./SourceFiles/stdafx.cpp \
    ./SourceFiles/apiwrap.cpp \
    ./SourceFiles/app.cpp \
    ./SourceFiles/application.cpp \
    ./SourceFiles/audio.cpp \
    ./SourceFiles/autoupdater.cpp \
    ./SourceFiles/dialogswidget.cpp \
    ./SourceFiles/dropdown.cpp \
    ./SourceFiles/fileuploader.cpp \
    ./SourceFiles/history.cpp \
    ./SourceFiles/historywidget.cpp \
    ./SourceFiles/lang.cpp \
    ./SourceFiles/langloaderplain.cpp \
    ./SourceFiles/layerwidget.cpp \
    ./SourceFiles/mediaview.cpp \
    ./SourceFiles/overviewwidget.cpp \
    ./SourceFiles/passcodewidget.cpp \
    ./SourceFiles/profilewidget.cpp \
    ./SourceFiles/playerwidget.cpp \
    ./SourceFiles/localimageloader.cpp \
    ./SourceFiles/localstorage.cpp \
    ./SourceFiles/logs.cpp \
    ./SourceFiles/mainwidget.cpp \
    ./SourceFiles/settings.cpp \
    ./SourceFiles/settingswidget.cpp \
    ./SourceFiles/structs.cpp \
    ./SourceFiles/sysbuttons.cpp \
    ./SourceFiles/title.cpp \
    ./SourceFiles/types.cpp \
    ./SourceFiles/window.cpp \
    ./SourceFiles/mtproto/mtp.cpp \
    ./SourceFiles/mtproto/mtpConnection.cpp \
    ./SourceFiles/mtproto/mtpCoreTypes.cpp \
    ./SourceFiles/mtproto/mtpDC.cpp \
    ./SourceFiles/mtproto/mtpFileLoader.cpp \
    ./SourceFiles/mtproto/mtpRPC.cpp \
    ./SourceFiles/mtproto/mtpScheme.cpp \
    ./SourceFiles/mtproto/mtpSession.cpp \
    ./SourceFiles/gui/animation.cpp \
    ./SourceFiles/gui/boxshadow.cpp \
    ./SourceFiles/gui/button.cpp \
    ./SourceFiles/gui/contextmenu.cpp \
    ./SourceFiles/gui/countryinput.cpp \
    ./SourceFiles/gui/emoji_config.cpp \
    ./SourceFiles/gui/filedialog.cpp \
    ./SourceFiles/gui/flatbutton.cpp \
    ./SourceFiles/gui/flatcheckbox.cpp \
    ./SourceFiles/gui/flatinput.cpp \
    ./SourceFiles/gui/flatlabel.cpp \
    ./SourceFiles/gui/flattextarea.cpp \
    ./SourceFiles/gui/images.cpp \
    ./SourceFiles/gui/scrollarea.cpp \
    ./SourceFiles/gui/style_core.cpp \
    ./SourceFiles/gui/text.cpp \
    ./SourceFiles/gui/twidget.cpp \
    ./SourceFiles/gui/switcher.cpp \
    ./GeneratedFiles/lang_auto.cpp \
    ./GeneratedFiles/style_auto.cpp \
    ./GeneratedFiles/numbers.cpp \
    ./SourceFiles/boxes/aboutbox.cpp \
    ./SourceFiles/boxes/abstractbox.cpp \
    ./SourceFiles/boxes/addcontactbox.cpp \
    ./SourceFiles/boxes/autolockbox.cpp \
    ./SourceFiles/boxes/backgroundbox.cpp \
    ./SourceFiles/boxes/confirmbox.cpp \
    ./SourceFiles/boxes/connectionbox.cpp \
    ./SourceFiles/boxes/contactsbox.cpp \
    ./SourceFiles/boxes/downloadpathbox.cpp \
    ./SourceFiles/boxes/emojibox.cpp \
    ./SourceFiles/boxes/languagebox.cpp \
    ./SourceFiles/boxes/passcodebox.cpp \
    ./SourceFiles/boxes/photocropbox.cpp \
    ./SourceFiles/boxes/photosendbox.cpp \
    ./SourceFiles/boxes/sessionsbox.cpp \
    ./SourceFiles/boxes/stickersetbox.cpp \
    ./SourceFiles/boxes/usernamebox.cpp \
    ./SourceFiles/intro/intro.cpp \
    ./SourceFiles/intro/introcode.cpp \
    ./SourceFiles/intro/introphone.cpp \
    ./SourceFiles/intro/intropwdcheck.cpp \
    ./SourceFiles/intro/introsignup.cpp \
    ./SourceFiles/intro/introsteps.cpp

HEADERS += \
    ./SourceFiles/stdafx.h \
    ./SourceFiles/apiwrap.h \
    ./SourceFiles/app.h \
    ./SourceFiles/application.h \
    ./SourceFiles/audio.h \
    ./SourceFiles/autoupdater.h \
    ./SourceFiles/config.h \
    ./SourceFiles/countries.h \
    ./SourceFiles/dialogswidget.h \
    ./SourceFiles/dropdown.h \
    ./SourceFiles/fileuploader.h \
    ./SourceFiles/history.h \
    ./SourceFiles/historywidget.h \
    ./SourceFiles/lang.h \
    ./SourceFiles/langloaderplain.h \
    ./SourceFiles/layerwidget.h \
    ./SourceFiles/mediaview.h \
    ./SourceFiles/numbers.h \
    ./SourceFiles/overviewwidget.h \
    ./SourceFiles/passcodewidget.h \
    ./SourceFiles/profilewidget.h \
    ./SourceFiles/playerwidget.h \
    ./SourceFiles/localimageloader.h \
    ./SourceFiles/localstorage.h \
    ./SourceFiles/logs.h \
    ./SourceFiles/mainwidget.h \
    ./SourceFiles/settings.h \
    ./SourceFiles/settingswidget.h \
    ./SourceFiles/structs.h \
    ./SourceFiles/style.h \
    ./SourceFiles/sysbuttons.h \
    ./SourceFiles/title.h \
    ./SourceFiles/types.h \
    ./SourceFiles/window.h \
    ./SourceFiles/mtproto/mtpSessionImpl.h \
    ./SourceFiles/mtproto/mtp.h \
    ./SourceFiles/mtproto/mtpAuthKey.h \
    ./SourceFiles/mtproto/mtpConnection.h \
    ./SourceFiles/mtproto/mtpCoreTypes.h \
    ./SourceFiles/mtproto/mtpDC.h \
    ./SourceFiles/mtproto/mtpFileLoader.h \
    ./SourceFiles/mtproto/mtpPublicRSA.h \
    ./SourceFiles/mtproto/mtpRPC.h \
    ./SourceFiles/mtproto/mtpScheme.h \
    ./SourceFiles/mtproto/mtpSession.h \
    ./SourceFiles/pspecific.h \
    ./SourceFiles/gui/animation.h \
    ./SourceFiles/gui/boxshadow.h \
    ./SourceFiles/gui/button.h \
    ./SourceFiles/gui/contextmenu.h \
    ./SourceFiles/gui/countryinput.h \
    ./SourceFiles/gui/emoji_config.h \
    ./SourceFiles/gui/filedialog.h \
    ./SourceFiles/gui/flatbutton.h \
    ./SourceFiles/gui/flatcheckbox.h \
    ./SourceFiles/gui/flatinput.h \
    ./SourceFiles/gui/flatlabel.h \
    ./SourceFiles/gui/flattextarea.h \
    ./SourceFiles/gui/images.h \
    ./SourceFiles/gui/scrollarea.h \
    ./SourceFiles/gui/style_core.h \
    ./SourceFiles/gui/text.h \
    ./SourceFiles/gui/twidget.h \
    ./SourceFiles/gui/switcher.h \
    ./GeneratedFiles/lang_auto.h \
    ./GeneratedFiles/style_auto.h \
    ./GeneratedFiles/style_classes.h \
    ./SourceFiles/boxes/aboutbox.h \
    ./SourceFiles/boxes/abstractbox.h \
    ./SourceFiles/boxes/addcontactbox.h \
    ./SourceFiles/boxes/autolockbox.h \
    ./SourceFiles/boxes/backgroundbox.h \
    ./SourceFiles/boxes/confirmbox.h \
    ./SourceFiles/boxes/connectionbox.h \
    ./SourceFiles/boxes/contactsbox.h \
    ./SourceFiles/boxes/downloadpathbox.h \
    ./SourceFiles/boxes/emojibox.h \
    ./SourceFiles/boxes/languagebox.h \
    ./SourceFiles/boxes/passcodebox.h \
    ./SourceFiles/boxes/photocropbox.h \
    ./SourceFiles/boxes/photosendbox.h \
    ./SourceFiles/boxes/sessionsbox.h \
    ./SourceFiles/boxes/stickersetbox.h \
    ./SourceFiles/boxes/usernamebox.h \
    ./SourceFiles/intro/intro.h \
    ./SourceFiles/intro/introcode.h \
    ./SourceFiles/intro/introphone.h \
    ./SourceFiles/intro/intropwdcheck.h \
    ./SourceFiles/intro/introsignup.h \
    ./SourceFiles/intro/introsteps.h

win32 {
SOURCES += \
  ./SourceFiles/pspecific_wnd.cpp
HEADERS += \
  ./SourceFiles/pspecific_wnd.h
}

macx {
SOURCES += \
  ./SourceFiles/pspecific_mac.cpp
HEADERS += \
  ./SourceFiles/pspecific_mac.h
}

CONFIG += precompile_header

PRECOMPILED_HEADER = ./SourceFiles/stdafx.h

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-result -Wno-unused-parameter -Wno-unused-variable -Wno-switch -Wno-comment -Wno-unused-but-set-variable

CONFIG(release, debug|release) {
    QMAKE_CXXFLAGS_RELEASE -= -O2
    QMAKE_CXXFLAGS_RELEASE += -Ofast -flto -fno-strict-aliasing
    QMAKE_LFLAGS_RELEASE -= -O1
    QMAKE_LFLAGS_RELEASE += -Ofast -flto
}

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.5.0/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.5.0/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\
               /usr/local/include/opus\
               ./SourceFiles\
               ./GeneratedFiles

INCLUDEPATH += "/usr/include/libappindicator-0.1"
INCLUDEPATH += "/usr/include/gtk-2.0"
INCLUDEPATH += "/usr/include/glib-2.0"
INCLUDEPATH += "/usr/lib/x86_64-linux-gnu/glib-2.0/include"
INCLUDEPATH += "/usr/lib/i386-linux-gnu/glib-2.0/include"
INCLUDEPATH += "/usr/include/cairo"
INCLUDEPATH += "/usr/include/pango-1.0"
INCLUDEPATH += "/usr/lib/x86_64-linux-gnu/gtk-2.0/include"
INCLUDEPATH += "/usr/lib/i386-linux-gnu/gtk-2.0/include"
INCLUDEPATH += "/usr/include/gdk-pixbuf-2.0"
INCLUDEPATH += "/usr/include/atk-1.0"

INCLUDEPATH += "/usr/include/dee-1.0"
INCLUDEPATH += "/usr/include/libdbusmenu-glib-0.4"

LIBS += -lcrypto -lssl -lz -ldl -llzma -lexif -lopenal -lavformat -lavcodec -lswresample -lavutil -lopus
LIBS += ./../../../Libraries/QtStatic/qtbase/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.a \
        ./../../../Libraries/QtStatic/qtbase/plugins/platforminputcontexts/libibusplatforminputcontextplugin.a \
        ./../../../Libraries/QtStatic/qtbase/plugins/platforminputcontexts/libfcitxplatforminputcontextplugin.a
LIBS += /usr/local/lib/libxkbcommon.a

RESOURCES += \
    ./SourceFiles/telegram.qrc \
    ./SourceFiles/telegram_linux.qrc \
    ./SourceFiles/telegram_emojis.qrc

OTHER_FILES += \
    Resources/style_classes.txt \
    Resources/style.txt \
    Resources/lang.strings \
    SourceFiles/langs/lang_it.strings \
    SourceFiles/langs/lang_es.strings \
    SourceFiles/langs/lang_de.strings \
    SourceFiles/langs/lang_nl.strings \
    SourceFiles/langs/lang_pt_BR.strings
