QT += core gui network widgets

CONFIG += plugin static c++14

CONFIG(debug, debug|release) {
	DEFINES += _DEBUG
	OBJECTS_DIR = ./../DebugIntermediate
	MOC_DIR = ./GeneratedFiles/Debug
	RCC_DIR = ./GeneratedFiles
	DESTDIR = ./../Debug
}
CONFIG(release, debug|release) {
	DEFINES += CUSTOM_API_ID
	OBJECTS_DIR = ./../ReleaseIntermediate
	MOC_DIR = ./GeneratedFiles/Release
	RCC_DIR = ./GeneratedFiles
	DESTDIR = ./../Release
}

macx {
	QMAKE_INFO_PLIST = ./SourceFiles/Telegram.plist
	QMAKE_LFLAGS += -framework Cocoa
}

linux {
	SOURCES += ./SourceFiles/pspecific_linux.cpp
	HEADERS += ./SourceFiles/pspecific_linux.h
}

CONFIG(debug, debug|release) {
	codegen_style.target = style_target
	codegen_style.depends = FORCE
	codegen_style.commands = ./../codegen/Debug/codegen_style "-I./../../Telegram/Resources" "-I./../../Telegram/SourceFiles" "-o./GeneratedFiles/styles" all_files.style --rebuild

	codegen_numbers.target = numbers_target
	codegen_numbers.depends = ./../../Telegram/Resources/numbers.txt
	codegen_numbers.commands = ./../codegen/Debug/codegen_numbers "-o./GeneratedFiles" "./../../Telegram/Resources/numbers.txt"

	codegen_numbers.commands = cd ../../Telegram && ./../Linux/codegen/Debug/codegen_numbers "-o./../Linux/DebugIntermediate/GeneratedFiles" "./Resources/numbers.txt" && cd ../Linux/DebugIntermediate

	codegen_lang.target = lang_target
	codegen_lang.depends = ./../../Telegram/Resources/langs/lang.strings
	codegen_lang.commands = mkdir -p ./GeneratedFiles && ./../DebugLang/MetaLang -lang_in ./../../Telegram/Resources/langs/lang.strings -lang_out ./GeneratedFiles/lang_auto
}

CONFIG(release, debug|release) {
	codegen_style.target = style_target
	codegen_style.depends = FORCE
	codegen_style.commands = ./../codegen/Release/codegen_style "-I./../../Telegram/Resources" "-I./../../Telegram/SourceFiles" "-o./GeneratedFiles/styles" all_files.style --rebuild

	codegen_numbers.target = numbers_target
	codegen_numbers.depends = ./../../Telegram/Resources/numbers.txt
	codegen_numbers.commands = ./../codegen/Release/codegen_numbers "-o./GeneratedFiles" "./../../Telegram/Resources/numbers.txt"

	codegen_numbers.commands = cd ../../Telegram && ./../Linux/codegen/Release/codegen_numbers "-o./../Linux/ReleaseIntermediate/GeneratedFiles" "./Resources/numbers.txt" && cd ../Linux/ReleaseIntermediate

	codegen_lang.target = lang_target
	codegen_lang.depends = ./../../Telegram/Resources/langs/lang.strings
	codegen_lang.commands = mkdir -p ./GeneratedFiles && ./../ReleaseLang/MetaLang -lang_in ./../../Telegram/Resources/langs/lang.strings -lang_out ./GeneratedFiles/lang_auto
}

file_style_basic.target = GeneratedFiles/styles/style_basic.cpp
file_style_basic.depends = style_target
file_style_basic_types.target = GeneratedFiles/styles/style_basic_types.cpp
file_style_basic_types.depends = style_target
file_style_overview.target = GeneratedFiles/styles/style_overview.cpp
file_style_overview.depends = style_target
file_style_dialogs.target = GeneratedFiles/styles/style_dialogs.cpp
file_style_dialogs.depends = style_target
file_style_history.target = GeneratedFiles/styles/style_history.cpp
file_style_history.depends = style_target
file_style_profile.target = GeneratedFiles/styles/style_profile.cpp
file_style_profile.depends = style_target

QMAKE_EXTRA_TARGETS += codegen_style codegen_numbers codegen_lang \
	file_style_basic file_style_basic_types file_style_overview \
	file_style_dialogs file_style_history file_style_profile

PRE_TARGETDEPS += style_target numbers_target lang_target

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
	./GeneratedFiles/lang_auto.cpp \
	./GeneratedFiles/numbers.cpp \
	./GeneratedFiles/styles/style_basic.cpp \
	./GeneratedFiles/styles/style_basic_types.cpp \
	./GeneratedFiles/styles/style_dialogs.cpp \
	./GeneratedFiles/styles/style_history.cpp \
	./GeneratedFiles/styles/style_overview.cpp \
	./GeneratedFiles/styles/style_profile.cpp \
	./SourceFiles/main.cpp \
	./SourceFiles/stdafx.cpp \
	./SourceFiles/apiwrap.cpp \
	./SourceFiles/app.cpp \
	./SourceFiles/application.cpp \
	./SourceFiles/audio.cpp \
	./SourceFiles/autoupdater.cpp \
	./SourceFiles/dialogswidget.cpp \
	./SourceFiles/dropdown.cpp \
	./SourceFiles/facades.cpp \
	./SourceFiles/fileuploader.cpp \
	./SourceFiles/history.cpp \
	./SourceFiles/historywidget.cpp \
	./SourceFiles/lang.cpp \
	./SourceFiles/langloaderplain.cpp \
	./SourceFiles/layerwidget.cpp \
	./SourceFiles/layout.cpp \
	./SourceFiles/mediaview.cpp \
	./SourceFiles/observer_peer.cpp \
	./SourceFiles/overviewwidget.cpp \
	./SourceFiles/passcodewidget.cpp \
	./SourceFiles/playerwidget.cpp \
	./SourceFiles/localimageloader.cpp \
	./SourceFiles/localstorage.cpp \
	./SourceFiles/logs.cpp \
	./SourceFiles/mainwidget.cpp \
	./SourceFiles/settings.cpp \
	./SourceFiles/settingswidget.cpp \
	./SourceFiles/shortcuts.cpp \
	./SourceFiles/structs.cpp \
	./SourceFiles/sysbuttons.cpp \
	./SourceFiles/title.cpp \
	./SourceFiles/mainwindow.cpp \
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
	./SourceFiles/boxes/report_box.cpp \
	./SourceFiles/boxes/sessionsbox.cpp \
	./SourceFiles/boxes/stickersetbox.cpp \
	./SourceFiles/boxes/usernamebox.cpp \
	./SourceFiles/core/basic_types.cpp \
	./SourceFiles/core/click_handler.cpp \
	./SourceFiles/core/click_handler_types.cpp \
	./SourceFiles/core/observer.cpp \
	./SourceFiles/data/data_abstract_structure.cpp \
	./SourceFiles/data/data_drafts.cpp \
	./SourceFiles/dialogs/dialogs_indexed_list.cpp \
	./SourceFiles/dialogs/dialogs_layout.cpp \
	./SourceFiles/dialogs/dialogs_list.cpp \
	./SourceFiles/dialogs/dialogs_row.cpp \
	./SourceFiles/history/field_autocomplete.cpp \
	./SourceFiles/history/history_service_layout.cpp \
	./SourceFiles/inline_bots/inline_bot_layout_internal.cpp \
	./SourceFiles/inline_bots/inline_bot_layout_item.cpp \
	./SourceFiles/inline_bots/inline_bot_result.cpp \
	./SourceFiles/inline_bots/inline_bot_send_data.cpp \
	./SourceFiles/intro/introwidget.cpp \
	./SourceFiles/intro/introcode.cpp \
	./SourceFiles/intro/introphone.cpp \
	./SourceFiles/intro/intropwdcheck.cpp \
	./SourceFiles/intro/introsignup.cpp \
	./SourceFiles/intro/introstart.cpp \
	./SourceFiles/mtproto/facade.cpp \
	./SourceFiles/mtproto/auth_key.cpp \
	./SourceFiles/mtproto/connection.cpp \
	./SourceFiles/mtproto/connection_abstract.cpp \
	./SourceFiles/mtproto/connection_auto.cpp \
	./SourceFiles/mtproto/connection_http.cpp \
	./SourceFiles/mtproto/connection_tcp.cpp \
	./SourceFiles/mtproto/core_types.cpp \
	./SourceFiles/mtproto/dcenter.cpp \
	./SourceFiles/mtproto/file_download.cpp \
	./SourceFiles/mtproto/rsa_public_key.cpp \
	./SourceFiles/mtproto/rpc_sender.cpp \
	./SourceFiles/mtproto/scheme_auto.cpp \
	./SourceFiles/mtproto/session.cpp \
	./SourceFiles/overview/overview_layout.cpp \
	./SourceFiles/platform/linux/linux_gdk_helper.cpp \
	./SourceFiles/platform/linux/linux_libs.cpp \
	./SourceFiles/platform/linux/file_dialog_linux.cpp \
	./SourceFiles/platform/linux/main_window_linux.cpp \
	./SourceFiles/profile/profile_actions_widget.cpp \
	./SourceFiles/profile/profile_block_widget.cpp \
	./SourceFiles/profile/profile_cover_drop_area.cpp \
	./SourceFiles/profile/profile_cover.cpp \
	./SourceFiles/profile/profile_fixed_bar.cpp \
	./SourceFiles/profile/profile_info_widget.cpp \
	./SourceFiles/profile/profile_inner_widget.cpp \
	./SourceFiles/profile/profile_invite_link_widget.cpp \
	./SourceFiles/profile/profile_members_widget.cpp \
	./SourceFiles/profile/profile_section_memento.cpp \
	./SourceFiles/profile/profile_settings_widget.cpp \
	./SourceFiles/profile/profile_shared_media_widget.cpp \
	./SourceFiles/profile/profile_userpic_button.cpp \
	./SourceFiles/profile/profile_widget.cpp \
	./SourceFiles/serialize/serialize_common.cpp \
	./SourceFiles/serialize/serialize_document.cpp \
	./SourceFiles/ui/buttons/history_down_button.cpp \
	./SourceFiles/ui/buttons/left_outline_button.cpp \
	./SourceFiles/ui/buttons/peer_avatar_button.cpp \
	./SourceFiles/ui/buttons/round_button.cpp \
	./SourceFiles/ui/style/style_core.cpp \
	./SourceFiles/ui/style/style_core_color.cpp \
	./SourceFiles/ui/style/style_core_font.cpp \
	./SourceFiles/ui/style/style_core_icon.cpp \
	./SourceFiles/ui/style/style_core_types.cpp \
	./SourceFiles/ui/text/text.cpp \
	./SourceFiles/ui/text/text_block.cpp \
	./SourceFiles/ui/text/text_entity.cpp \
	./SourceFiles/ui/toast/toast.cpp \
	./SourceFiles/ui/toast/toast_manager.cpp \
	./SourceFiles/ui/toast/toast_widget.cpp \
	./SourceFiles/ui/animation.cpp \
	./SourceFiles/ui/boxshadow.cpp \
	./SourceFiles/ui/button.cpp \
	./SourceFiles/ui/popupmenu.cpp \
	./SourceFiles/ui/countryinput.cpp \
	./SourceFiles/ui/emoji_config.cpp \
	./SourceFiles/ui/filedialog.cpp \
	./SourceFiles/ui/flatbutton.cpp \
	./SourceFiles/ui/flatcheckbox.cpp \
	./SourceFiles/ui/flatinput.cpp \
	./SourceFiles/ui/flatlabel.cpp \
	./SourceFiles/ui/flattextarea.cpp \
	./SourceFiles/ui/images.cpp \
	./SourceFiles/ui/inner_dropdown.cpp \
	./SourceFiles/ui/scrollarea.cpp \
	./SourceFiles/ui/twidget.cpp \
	./SourceFiles/window/main_window.cpp \
	./SourceFiles/window/section_widget.cpp \
	./SourceFiles/window/slide_animation.cpp \
	./SourceFiles/window/top_bar_widget.cpp

HEADERS += \
	./GeneratedFiles/lang_auto.h \
	./GeneratedFiles/numbers.h \
	./GeneratedFiles/styles/style_basic.h \
	./GeneratedFiles/styles/style_basic_types.h \
	./GeneratedFiles/styles/style_dialogs.h \
	./GeneratedFiles/styles/style_history.h \
	./GeneratedFiles/styles/style_overview.h \
	./GeneratedFiles/styles/style_profile.h \
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
	./SourceFiles/facades.h \
	./SourceFiles/fileuploader.h \
	./SourceFiles/history.h \
	./SourceFiles/historywidget.h \
	./SourceFiles/lang.h \
	./SourceFiles/langloaderplain.h \
	./SourceFiles/layerwidget.h \
	./SourceFiles/layout.h \
	./SourceFiles/mediaview.h \
	./SourceFiles/observer_peer.h \
	./SourceFiles/overviewwidget.h \
	./SourceFiles/passcodewidget.h \
	./SourceFiles/playerwidget.h \
	./SourceFiles/localimageloader.h \
	./SourceFiles/localstorage.h \
	./SourceFiles/logs.h \
	./SourceFiles/mainwidget.h \
	./SourceFiles/settings.h \
	./SourceFiles/settingswidget.h \
	./SourceFiles/shortcuts.h \
	./SourceFiles/structs.h \
	./SourceFiles/sysbuttons.h \
	./SourceFiles/title.h \
	./SourceFiles/mainwindow.h \
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
	./SourceFiles/boxes/report_box.h \
	./SourceFiles/boxes/sessionsbox.h \
	./SourceFiles/boxes/stickersetbox.h \
	./SourceFiles/boxes/usernamebox.h \
	./SourceFiles/core/basic_types.h \
	./SourceFiles/core/click_handler.h \
	./SourceFiles/core/click_handler_types.h \
	./SourceFiles/core/observer.h \
	./SourceFiles/core/vector_of_moveable.h \
	./SourceFiles/core/version.h \
	./SourceFiles/data/data_abstract_structure.h \
	./SourceFiles/data/data_drafts.h \
	./SourceFiles/dialogs/dialogs_common.h \
	./SourceFiles/dialogs/dialogs_indexed_list.h \
	./SourceFiles/dialogs/dialogs_layout.h \
	./SourceFiles/dialogs/dialogs_list.h \
	./SourceFiles/dialogs/dialogs_row.h \
	./SourceFiles/history/field_autocomplete.h \
	./SourceFiles/history/history_common.h \
	./SourceFiles/history/history_service_layout.h \
	./SourceFiles/inline_bots/inline_bot_layout_internal.h \
	./SourceFiles/inline_bots/inline_bot_layout_item.h \
	./SourceFiles/inline_bots/inline_bot_result.h \
	./SourceFiles/inline_bots/inline_bot_send_data.h \
	./SourceFiles/intro/introwidget.h \
	./SourceFiles/intro/introcode.h \
	./SourceFiles/intro/introphone.h \
	./SourceFiles/intro/intropwdcheck.h \
	./SourceFiles/intro/introsignup.h \
	./SourceFiles/intro/introstart.h \
	./SourceFiles/mtproto/facade.h \
	./SourceFiles/mtproto/auth_key.h \
	./SourceFiles/mtproto/connection.h \
	./SourceFiles/mtproto/connection_abstract.h \
	./SourceFiles/mtproto/connection_auto.h \
	./SourceFiles/mtproto/connection_http.h \
	./SourceFiles/mtproto/connection_tcp.h \
	./SourceFiles/mtproto/core_types.h \
	./SourceFiles/mtproto/dcenter.h \
	./SourceFiles/mtproto/file_download.h \
	./SourceFiles/mtproto/rsa_public_key.h \
	./SourceFiles/mtproto/rpc_sender.h \
	./SourceFiles/mtproto/scheme_auto.h \
	./SourceFiles/mtproto/session.h \
	./SourceFiles/overview/overview_layout.h \
	./SourceFiles/platform/platform_file_dialog.h \
	./SourceFiles/platform/platform_main_window.h \
	./SourceFiles/platform/linux/linux_gdk_helper.h \
	./SourceFiles/platform/linux/linux_libs.h \
	./SourceFiles/platform/linux/file_dialog_linux.h \
	./SourceFiles/platform/linux/main_window_linux.h \
	./SourceFiles/profile/profile_actions_widget.h \
	./SourceFiles/profile/profile_block_widget.h \
	./SourceFiles/profile/profile_cover_drop_area.h \
	./SourceFiles/profile/profile_cover.h \
	./SourceFiles/profile/profile_fixed_bar.h \
	./SourceFiles/profile/profile_info_widget.h \
	./SourceFiles/profile/profile_inner_widget.h \
	./SourceFiles/profile/profile_invite_link_widget.h \
	./SourceFiles/profile/profile_members_widget.h \
	./SourceFiles/profile/profile_section_memento.h \
	./SourceFiles/profile/profile_settings_widget.h \
	./SourceFiles/profile/profile_shared_media_widget.h \
	./SourceFiles/profile/profile_userpic_button.h \
	./SourceFiles/profile/profile_widget.h \
	./SourceFiles/pspecific.h \
	./SourceFiles/serialize/serialize_common.h \
	./SourceFiles/serialize/serialize_document.h \
	./SourceFiles/ui/buttons/history_down_button.h \
	./SourceFiles/ui/buttons/left_outline_button.h \
	./SourceFiles/ui/buttons/peer_avatar_button.h \
	./SourceFiles/ui/buttons/round_button.h \
	./SourceFiles/ui/style/style_core.h \
	./SourceFiles/ui/style/style_core_color.h \
	./SourceFiles/ui/style/style_core_font.h \
	./SourceFiles/ui/style/style_core_icon.h \
	./SourceFiles/ui/style/style_core_types.h \
	./SourceFiles/ui/text/text.h \
	./SourceFiles/ui/text/text_block.h \
	./SourceFiles/ui/text/text_entity.h \
	./SourceFiles/ui/toast/toast.h \
	./SourceFiles/ui/toast/toast_manager.h \
	./SourceFiles/ui/toast/toast_widget.h \
	./SourceFiles/ui/animation.h \
	./SourceFiles/ui/boxshadow.h \
	./SourceFiles/ui/button.h \
	./SourceFiles/ui/popupmenu.h \
	./SourceFiles/ui/countryinput.h \
	./SourceFiles/ui/emoji_config.h \
	./SourceFiles/ui/filedialog.h \
	./SourceFiles/ui/flatbutton.h \
	./SourceFiles/ui/flatcheckbox.h \
	./SourceFiles/ui/flatinput.h \
	./SourceFiles/ui/flatlabel.h \
	./SourceFiles/ui/flattextarea.h \
	./SourceFiles/ui/images.h \
	./SourceFiles/ui/inner_dropdown.h \
	./SourceFiles/ui/scrollarea.h \
	./SourceFiles/ui/twidget.h \
	./SourceFiles/window/main_window.h \
	./SourceFiles/window/section_memento.h \
	./SourceFiles/window/section_widget.h \
	./SourceFiles/window/slide_animation.h \
	./SourceFiles/window/top_bar_widget.h

win32 {
SOURCES += \
	./SourceFiles/pspecific_win.cpp \
	./SourceFiles/platform/win/windows_app_user_model_id.cpp \
	./SourceFiles/platform/win/windows_dlls.cpp \
	./SourceFiles/platform/win/windows_event_filter.cpp \
	./SourceFiles/platform/win/windows_toasts.cpp

HEADERS += \
	./SourceFiles/pspecific_win.h \
	./SourceFiles/platform/win/windows_app_user_model_id.h \
	./SourceFiles/platform/win/windows_dlls.h \
	./SourceFiles/platform/win/windows_event_filter.h \
	./SourceFiles/platform/win/windows_toasts.h
}

winrt {
SOURCES += \
	./SourceFiles/pspecific_winrt.cpp \
	./SourceFiles/platform/winrt/main_window_winrt.cpp
HEADERS += \
	./SourceFiles/pspecific_winrt.h \
	./Sourcefiles/platform/winrt/main_window_winrt.h
}

macx {
SOURCES += \
	./SourceFiles/pspecific_mac.cpp
HEADERS += \
	./SourceFiles/pspecific_mac.h
OBJECTIVE_SOURCES += \
	./SourceFiles/pspecific_mac_p.mm \
	./SourceFiles/platform/mac/main_window_mac.mm
HEADERS += \
	./SourceFiles/pspecific_mac_p.h \
	./SourceFiles/platform/mac/main_window_mac.h
}

SOURCES += \
	./ThirdParty/minizip/zip.c \
	./ThirdParty/minizip/ioapi.c

CONFIG += precompile_header

PRECOMPILED_HEADER = ./SourceFiles/stdafx.h

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-result -Wno-unused-parameter -Wno-unused-variable -Wno-switch -Wno-comment -Wno-unused-but-set-variable
QMAKE_CFLAGS_WARN_ON += -Wno-unused-result -Wno-unused-parameter -Wno-unused-variable -Wno-switch -Wno-comment -Wno-unused-but-set-variable

CONFIG(release, debug|release) {
	QMAKE_CXXFLAGS_RELEASE -= -O2
	QMAKE_CXXFLAGS_RELEASE += -Ofast -flto -fno-strict-aliasing -g
	QMAKE_LFLAGS_RELEASE -= -O1
	QMAKE_LFLAGS_RELEASE += -Ofast -flto -g -rdynamic -static-libstdc++
}
# Linux 32bit fails Release link with Link-Time Optimization: virtual memory exhausted
unix {
	!contains(QMAKE_TARGET.arch, x86_64) {
		CONFIG(release, debug|release) {
			QMAKE_CXXFLAGS_RELEASE -= -flto
			QMAKE_LFLAGS_RELEASE -= -flto
		}
	}
}
CONFIG(debug, debug|release) {
	QMAKE_LFLAGS_DEBUG += -g -rdynamic -static-libstdc++
}

include(qt_static.pri)

INCLUDEPATH += \
	./SourceFiles\
	./GeneratedFiles\
	./ThirdParty/minizip\
	./../../Libraries/breakpad/src

CONFIG += link_pkgconfig
PKG_CONFIG = $$pkgConfigExecutable()

# include dirs only
QMAKE_CXXFLAGS += `$$PKG_CONFIG --cflags appindicator-0.1`
QMAKE_CXXFLAGS += `$$PKG_CONFIG --cflags gtk+-2.0`
QMAKE_CXXFLAGS += `$$PKG_CONFIG --cflags glib-2.0`
QMAKE_CXXFLAGS += `$$PKG_CONFIG --cflags dee-1.0`

# include dirs and libraries
PKGCONFIG += \
	x11\
	xi\
	xext\
	xkbcommon\
	openal\
	libavformat\
	libavcodec\
	libswresample\
	libswscale\
	libavutil\
	opus\
	libva\
	libssl\
	libcrypto\
	zlib\
	liblzma

LIBS += -ldl

LIBS += $${QT_TDESKTOP_PATH}/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.a \
        $${QT_TDESKTOP_PATH}/plugins/platforminputcontexts/libibusplatforminputcontextplugin.a \
        $${QT_TDESKTOP_PATH}/plugins/platforminputcontexts/libfcitxplatforminputcontextplugin.a

LIBS += ./../../../Libraries/breakpad/src/client/linux/libbreakpad_client.a

RESOURCES += \
	./Resources/telegram.qrc \
	./Resources/telegram_linux.qrc \
	./Resources/telegram_emojis.qrc

OTHER_FILES += \
	./Resources/basic_types.style \
	./Resources/basic.style \
	./Resources/all_files.style \
	./Resources/langs/lang.strings \
	./Resources/langs/lang_it.strings \
	./Resources/langs/lang_es.strings \
	./Resources/langs/lang_de.strings \
	./Resources/langs/lang_nl.strings \
	./Resources/langs/lang_pt_BR.strings \
	./SourceFiles/dialogs/dialogs.style \
	./SourceFiles/history/history.style \
	./SourceFiles/overview/overview.style \
	./SourceFiles/profile/profile.style
