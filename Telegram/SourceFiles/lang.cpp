/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "lang.h"

LangString langCounted(ushort key0, ushort tag, float64 value) { // current lang dependent
	int v = qFloor(value);
	QString sv;
	ushort key = key0;
	if (v != qCeil(value)) {
		key += 2;
		sv = QString::number(value);
	} else {
		if (v == 1) {
			key += 1;
		} else if (v) {
			key += 2;
		}
		sv = QString::number(v);
	}
	while (key > key0) {
		LangString v = lang(LangKey(key));
		if (!v.isEmpty()) return v.tag(tag, sv);
		--key;
	}
	return lang(LangKey(key0)).tag(tag, sv);
}

const QString &LangLoader::errors() const {
	if (_errors.isEmpty() && !_err.isEmpty()) {
		_errors = _err.join('\n');
	}
	return _errors;
}

const QString &LangLoader::warnings() const {
	if (!_checked) {
		for (int32 i = 0; i < lngkeys_cnt; ++i) {
			if (!_found[i]) {
				_warn.push_back(qsl("No value found for key '%1'").arg(langKeyName(LangKey(i))));
			}
		}
		_checked = true;
	}
	if (_warnings.isEmpty() && !_warn.isEmpty()) {
		_warnings = _warn.join('\n');
	}
	return _warnings;
}

void LangLoader::foundKeyValue(LangKey key) {
	if (key < lngkeys_cnt) {
		_found[key] = true;
	}
}

QString Translator::translate(const char *context, const char *sourceText, const char *disambiguation, int n) const {
	if (QLatin1String("QMenuBar") == context) {
		if (QLatin1String("Services") == sourceText) return lang(lng_mac_menu_services);
		if (QLatin1String("Hide %1") == sourceText) return lng_mac_menu_hide_telegram(lt_telegram, qsl("%1"));
		if (QLatin1String("Hide Others") == sourceText) return lang(lng_mac_menu_hide_others);
		if (QLatin1String("Show All") == sourceText) return lang(lng_mac_menu_show_all);
		if (QLatin1String("Preferences...") == sourceText) return lang(lng_mac_menu_preferences);
		if (QLatin1String("Quit %1") == sourceText) return lng_mac_menu_quit_telegram(lt_telegram, qsl("%1"));
		if (QLatin1String("About %1") == sourceText) return lng_mac_menu_about_telegram(lt_telegram, qsl("%1"));
		return QString();
	}
	if (QLatin1String("QWidgetTextControl") == context || QLatin1String("QLineEdit") == context) {
		if (QLatin1String("&Undo") == sourceText) return lang((cPlatform() == dbipWindows) ? lng_wnd_menu_undo : ((cPlatform() == dbipMac) ? lng_mac_menu_undo : lng_linux_menu_undo));
		if (QLatin1String("&Redo") == sourceText) return lang((cPlatform() == dbipWindows) ? lng_wnd_menu_redo : ((cPlatform() == dbipMac) ? lng_mac_menu_redo : lng_linux_menu_redo));
		if (QLatin1String("Cu&t") == sourceText) return lang(lng_mac_menu_cut);
		if (QLatin1String("&Copy") == sourceText) return lang(lng_mac_menu_copy);
		if (QLatin1String("&Paste") == sourceText) return lang(lng_mac_menu_paste);
		if (QLatin1String("Delete") == sourceText) return lang(lng_mac_menu_delete);
		if (QLatin1String("Select All") == sourceText) return lang(lng_mac_menu_select_all);
		return QString();
	}
	if (QLatin1String("QUnicodeControlCharacterMenu") == context) {
		if (QLatin1String("Insert Unicode control character") == sourceText) return lang(lng_menu_insert_unicode);
		return QString();
	}
	return QString();
}
