QT += core gui widgets network multimedia

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Mac/DebugIntermediate
    MOC_DIR = ./GeneratedFiles/Debug
    RCC_DIR = ./GeneratedFiles
    DESTDIR = ./../Mac/Debug
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Mac/ReleaseIntermediate
    MOC_DIR = ./GeneratedFiles/Release
    RCC_DIR = ./GeneratedFiles
    DESTDIR = ./../Mac/Release
}

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/Telegram.plist
    OBJECTIVE_SOURCES += ./SourceFiles/pspecific_mac_p.mm
    OBJECTIVE_HEADERS += ./SourceFiles/pspecific_mac_p.h
    QMAKE_LFLAGS += -framework Cocoa
}

SOURCES += \
    ./SourceFiles/main.cpp \
    ./SourceFiles/stdafx.cpp \
    ./SourceFiles/app.cpp \
    ./SourceFiles/application.cpp \
    ./SourceFiles/dialogswidget.cpp \
    ./SourceFiles/dropdown.cpp \
    ./SourceFiles/fileuploader.cpp \
    ./SourceFiles/history.cpp \
    ./SourceFiles/historywidget.cpp \
    ./SourceFiles/langloaderplain.cpp \
    ./SourceFiles/layerwidget.cpp \
    ./SourceFiles/profilewidget.cpp \
    ./SourceFiles/localimageloader.cpp \
    ./SourceFiles/logs.cpp \
    ./SourceFiles/mainwidget.cpp \
    ./SourceFiles/settings.cpp \
    ./SourceFiles/settingswidget.cpp \
    ./SourceFiles/supporttl.cpp \
    ./SourceFiles/sysbuttons.cpp \
    ./SourceFiles/title.cpp \
    ./SourceFiles/types.cpp \
    ./SourceFiles/window.cpp \
    ./SourceFiles/mtproto/mtp.cpp \
    ./SourceFiles/mtproto/mtpConnection.cpp \
    ./SourceFiles/mtproto/mtpDC.cpp \
    ./SourceFiles/mtproto/mtpFileLoader.cpp \
    ./SourceFiles/mtproto/mtpRPC.cpp \
    ./SourceFiles/mtproto/mtpSession.cpp \
    ./SourceFiles/gui/animation.cpp \
    ./SourceFiles/gui/boxshadow.cpp \
    ./SourceFiles/gui/button.cpp \
    ./SourceFiles/gui/countrycodeinput.cpp \
    ./SourceFiles/gui/countryinput.cpp \
    ./SourceFiles/gui/emoji_config.cpp \
    ./SourceFiles/gui/filedialog.cpp \
    ./SourceFiles/gui/flatbutton.cpp \
    ./SourceFiles/gui/flatcheckbox.cpp \
    ./SourceFiles/gui/flatinput.cpp \
    ./SourceFiles/gui/flatlabel.cpp \
    ./SourceFiles/gui/flattextarea.cpp \
    ./SourceFiles/gui/images.cpp \
    ./SourceFiles/gui/phoneinput.cpp \
    ./SourceFiles/gui/scrollarea.cpp \
    ./SourceFiles/gui/style_core.cpp \
    ./SourceFiles/gui/text.cpp \
    ./SourceFiles/gui/twidget.cpp \
    ./GeneratedFiles/lang.cpp \
    ./GeneratedFiles/style_auto.cpp \
    ./SourceFiles/boxes/aboutbox.cpp \
    ./SourceFiles/boxes/addcontactbox.cpp \
    ./SourceFiles/boxes/addparticipantbox.cpp \
    ./SourceFiles/boxes/confirmbox.cpp \
    ./SourceFiles/boxes/connectionbox.cpp \
    ./SourceFiles/boxes/contactsbox.cpp \
    ./SourceFiles/boxes/downloadpathbox.cpp \
    ./SourceFiles/boxes/emojibox.cpp \
    ./SourceFiles/boxes/newgroupbox.cpp \
    ./SourceFiles/boxes/photocropbox.cpp \
    ./SourceFiles/boxes/photosendbox.cpp \
    ./SourceFiles/intro/intro.cpp \
    ./SourceFiles/intro/introcode.cpp \
    ./SourceFiles/intro/introphone.cpp \
    ./SourceFiles/intro/introsignup.cpp \
    ./SourceFiles/intro/introsteps.cpp

HEADERS += \
    ./SourceFiles/stdafx.h \
    ./SourceFiles/app.h \
    ./SourceFiles/application.h \
    ./SourceFiles/config.h \
    ./SourceFiles/countries.h \
    ./SourceFiles/dialogswidget.h \
    ./SourceFiles/dropdown.h \
    ./SourceFiles/fileuploader.h \
    ./SourceFiles/history.h \
    ./SourceFiles/historywidget.h \
    ./SourceFiles/langloaderplain.h \
    ./SourceFiles/layerwidget.h \
    ./SourceFiles/profilewidget.h \
    ./SourceFiles/localimageloader.h \
    ./SourceFiles/logs.h \
    ./SourceFiles/mainwidget.h \
    ./SourceFiles/settings.h \
    ./SourceFiles/settingswidget.h \
    ./SourceFiles/style.h \
    ./SourceFiles/supporttl.h \
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
    ./SourceFiles/gui/countrycodeinput.h \
    ./SourceFiles/gui/countryinput.h \
    ./SourceFiles/gui/emoji_config.h \
    ./SourceFiles/gui/filedialog.h \
    ./SourceFiles/gui/flatbutton.h \
    ./SourceFiles/gui/flatcheckbox.h \
    ./SourceFiles/gui/flatinput.h \
    ./SourceFiles/gui/flatlabel.h \
    ./SourceFiles/gui/flattextarea.h \
    ./SourceFiles/gui/images.h \
    ./SourceFiles/gui/phoneinput.h \
    ./SourceFiles/gui/scrollarea.h \
    ./SourceFiles/gui/style_core.h \
    ./SourceFiles/gui/text.h \
    ./SourceFiles/gui/twidget.h \
    ./GeneratedFiles/lang.h \
    ./GeneratedFiles/style_auto.h \
    ./GeneratedFiles/style_classes.h \
    ./SourceFiles/boxes/aboutbox.h \
    ./SourceFiles/boxes/addcontactbox.h \
    ./SourceFiles/boxes/addparticipantbox.h \
    ./SourceFiles/boxes/confirmbox.h \
    ./SourceFiles/boxes/connectionbox.h \
    ./SourceFiles/boxes/contactsbox.h \
    ./SourceFiles/boxes/downloadpathbox.h \
    ./SourceFiles/boxes/emojibox.h \
    ./SourceFiles/boxes/newgroupbox.h \
    ./SourceFiles/boxes/photocropbox.h \
    ./SourceFiles/boxes/photosendbox.h \
    ./SourceFiles/intro/intro.h \
    ./SourceFiles/intro/introcode.h \
    ./SourceFiles/intro/introphone.h \
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

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.0/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.0/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\
               ./SourceFiles\
               ./GeneratedFiles\
               ./../../Libraries/lzma/C\
               ./../../Libraries/libexif-0.6.20

LIBS += -lcrypto -lssl -lz
LIBS += ./../../Libraries/libexif-0.6.20/libexif/.libs/libexif.a

RESOURCES += \
    ./SourceFiles/telegram.qrc

