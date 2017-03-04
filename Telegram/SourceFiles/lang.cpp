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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "lang.h"

#include "langloaderplain.h"

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

//#define NEW_VER_TAG lt_link
//#define NEW_VER_TAG_VALUE "https://telegram.org/blog/desktop-1-0"

QString langNewVersionText() {
#ifdef NEW_VER_TAG
	return lng_new_version_text(NEW_VER_TAG, QString::fromUtf8(NEW_VER_TAG_VALUE));
#else // NEW_VER_TAG
	return lang(lng_new_version_text);
#endif // NEW_VER_TAG
}

#ifdef NEW_VER_TAG
#define NEW_VER_KEY lng_new_version_text__tagged
#define NEW_VER_POSTFIX .tag(NEW_VER_TAG, QString::fromUtf8(NEW_VER_TAG_VALUE))
#else // NEW_VER_TAG
#define NEW_VER_KEY lng_new_version_text
#define NEW_VER_POSTFIX
#endif // NEW_VER_TAG

QString langNewVersionTextForLang(int langId) {
	LangLoaderResult result;
	if (langId) {
		LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[langId].c_str() + qsl(".strings"), langLoaderRequest(lng_language_name, NEW_VER_KEY));
		result = loader.found();
	} else {
		result.insert(lng_language_name, langOriginal(lng_language_name));
		result.insert(NEW_VER_KEY, langOriginal(NEW_VER_KEY));
	}
	return result.value(lng_language_name, LanguageCodes[langId].c_str() + qsl(" language")) + qsl(":\n\n") + LangString(result.value(NEW_VER_KEY, qsl("--none--")))NEW_VER_POSTFIX;
}

#undef NEW_VER_POSTFIX
#undef NEW_VER_KEY

#undef NEW_VER_TAG_VALUE
#undef NEW_VER_TAG

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
	if (qstr("QMenuBar") == context) {
		if (qstr("Services") == sourceText) return lang(lng_mac_menu_services);
		if (qstr("Hide %1") == sourceText) return lng_mac_menu_hide_telegram(lt_telegram, qsl("%1"));
		if (qstr("Hide Others") == sourceText) return lang(lng_mac_menu_hide_others);
		if (qstr("Show All") == sourceText) return lang(lng_mac_menu_show_all);
		if (qstr("Preferences...") == sourceText) return lang(lng_mac_menu_preferences);
		if (qstr("Quit %1") == sourceText) return lng_mac_menu_quit_telegram(lt_telegram, qsl("%1"));
		if (qstr("About %1") == sourceText) return lng_mac_menu_about_telegram(lt_telegram, qsl("%1"));
		return QString();
	}
	if (qstr("QWidgetTextControl") == context || qstr("QLineEdit") == context) {
		if (qstr("&Undo") == sourceText) return lang((cPlatform() == dbipWindows) ? lng_wnd_menu_undo : ((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_mac_menu_undo : lng_linux_menu_undo));
		if (qstr("&Redo") == sourceText) return lang((cPlatform() == dbipWindows) ? lng_wnd_menu_redo : ((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_mac_menu_redo : lng_linux_menu_redo));
		if (qstr("Cu&t") == sourceText) return lang(lng_mac_menu_cut);
		if (qstr("&Copy") == sourceText) return lang(lng_mac_menu_copy);
		if (qstr("&Paste") == sourceText) return lang(lng_mac_menu_paste);
		if (qstr("Delete") == sourceText) return lang(lng_mac_menu_delete);
		if (qstr("Select All") == sourceText) return lang(lng_mac_menu_select_all);
		return QString();
	}
	if (qstr("QUnicodeControlCharacterMenu") == context) {
		if (qstr("Insert Unicode control character") == sourceText) return lang(lng_menu_insert_unicode);
		return QString();
	}
	return QString();
}
