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
	if (qstr("QMenuBar") == context) {
		if (qstr("Services") == sourceText) return tr::lng_mac_menu_services(tr::now);
		if (qstr("Hide %1") == sourceText) return tr::lng_mac_menu_hide_telegram(tr::now, lt_telegram, qsl("%1"));
		if (qstr("Hide Others") == sourceText) return tr::lng_mac_menu_hide_others(tr::now);
		if (qstr("Show All") == sourceText) return tr::lng_mac_menu_show_all(tr::now);
		if (qstr("Preferences...") == sourceText) return tr::lng_mac_menu_preferences(tr::now);
		if (qstr("Quit %1") == sourceText) return tr::lng_mac_menu_quit_telegram(tr::now, lt_telegram, qsl("%1"));
		if (qstr("About %1") == sourceText) return tr::lng_mac_menu_about_telegram(tr::now, lt_telegram, qsl("%1"));
		return QString();
	}
	if (qstr("QWidgetTextControl") == context || qstr("QLineEdit") == context) {
		if (qstr("&Undo") == sourceText) return Platform::IsWindows() ? tr::lng_wnd_menu_undo(tr::now) : Platform::IsMac() ? tr::lng_mac_menu_undo(tr::now) : tr::lng_linux_menu_undo(tr::now);
		if (qstr("&Redo") == sourceText) return Platform::IsWindows() ? tr::lng_wnd_menu_redo(tr::now) : Platform::IsMac() ? tr::lng_mac_menu_redo(tr::now) : tr::lng_linux_menu_redo(tr::now);
		if (qstr("Cu&t") == sourceText) return tr::lng_mac_menu_cut(tr::now);
		if (qstr("&Copy") == sourceText) return tr::lng_mac_menu_copy(tr::now);
		if (qstr("&Paste") == sourceText) return tr::lng_mac_menu_paste(tr::now);
		if (qstr("Delete") == sourceText) return tr::lng_mac_menu_delete(tr::now);
		if (qstr("Select All") == sourceText) return tr::lng_mac_menu_select_all(tr::now);
		return QString();
	}
	if (qstr("QUnicodeControlCharacterMenu") == context) {
		if (qstr("Insert Unicode control character") == sourceText) return tr::lng_menu_insert_unicode(tr::now);
		return QString();
	}
	return QString();
}

} // namespace Lang
