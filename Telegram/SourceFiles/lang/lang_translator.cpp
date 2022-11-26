/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_translator.h"

#include "lang/lang_keys.h"
#include "base/platform/base_platform_info.h"

namespace Lang {

QString Translator::translate(const char *context, const char *sourceText, const char *disambiguation, int n) const {
	if (u"QMenuBar"_q == context) {
		if (u"Services"_q == sourceText) return tr::lng_mac_menu_services(tr::now);
		if (u"Hide %1"_q == sourceText) return tr::lng_mac_menu_hide_telegram(tr::now, lt_telegram, u"%1"_q);
		if (u"Hide Others"_q == sourceText) return tr::lng_mac_menu_hide_others(tr::now);
		if (u"Show All"_q == sourceText) return tr::lng_mac_menu_show_all(tr::now);
		if (u"Preferences..."_q == sourceText) return tr::lng_mac_menu_preferences(tr::now);
		if (u"Quit %1"_q == sourceText) return tr::lng_mac_menu_quit_telegram(tr::now, lt_telegram, u"%1"_q);
		if (u"About %1"_q == sourceText) return tr::lng_mac_menu_about_telegram(tr::now, lt_telegram, u"%1"_q);
		return QString();
	}
	if (u"QWidgetTextControl"_q == context || u"QLineEdit"_q == context) {
		if (u"&Undo"_q == sourceText) return Platform::IsWindows() ? tr::lng_wnd_menu_undo(tr::now) : Platform::IsMac() ? tr::lng_mac_menu_undo(tr::now) : tr::lng_linux_menu_undo(tr::now);
		if (u"&Redo"_q == sourceText) return Platform::IsWindows() ? tr::lng_wnd_menu_redo(tr::now) : Platform::IsMac() ? tr::lng_mac_menu_redo(tr::now) : tr::lng_linux_menu_redo(tr::now);
		if (u"Cu&t"_q == sourceText) return tr::lng_mac_menu_cut(tr::now);
		if (u"&Copy"_q == sourceText) return tr::lng_mac_menu_copy(tr::now);
		if (u"&Paste"_q == sourceText) return tr::lng_mac_menu_paste(tr::now);
		if (u"Delete"_q == sourceText) return tr::lng_mac_menu_delete(tr::now);
		if (u"Select All"_q == sourceText) return tr::lng_mac_menu_select_all(tr::now);
		return QString();
	}
	if (u"QUnicodeControlCharacterMenu"_q == context) {
		if (u"Insert Unicode control character"_q == sourceText) return tr::lng_menu_insert_unicode(tr::now);
		return QString();
	}
	return QString();
}

} // namespace Lang
