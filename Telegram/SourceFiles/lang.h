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
#pragma once

enum Languages {
	langTestlang = -1,

	langEnglish = 0,
	langItalian,
	langSpanish,

	langCount
};

static const char *LanguageCodes[] = {
	"en",
	"it",
	"es",
};

static const char *TestLangFile = "testlang.strings";

class LangString : public QString {
public:
	LangString() {
	}
	LangString(const QString &str) : QString(str) {
	}

	LangString tag(ushort tag, const QString &replacement) {
		for (const QChar *s = constData(), *ch = s, *e = ch + size(); ch != e;) {
			if (*ch == TextCommand) {
				if (ch + 3 < e && (ch + 1)->unicode() == TextCommandLangTag && *(ch + 3) == TextCommand) {
					if ((ch + 2)->unicode() == 0x0020 + tag) {
						LangString result;
						result.reserve(size() + replacement.size() - 4);
						if (ch > s) result.append(midRef(0, ch - s));
						result.append(replacement);
						if (ch + 4 < e) result.append(midRef(ch - s + 4));
						return result;
					} else {
						ch += 4;
					}
				} else {
					const QChar *next = textSkipCommand(ch, e);
					if (next == ch) {
						++ch;
					} else {
						ch = next;
					}
				}
			} else {
				++ch;
			}
		}
		return *this;
	}

	LangString &operator=(const QString &str) {
		QString::operator=(str);
		return (*this);
	}

};

LangString langCounted(ushort key0, ushort tag, float64 value);

#include "lang_auto.h"

const char *langKeyName(LangKey key);

inline LangString langDayOfMonth(const QDate &date) {
	int32 month = date.month(), day = date.day();
	return (month > 0 && month <= 12) ? lng_month_day(lt_month, lang(LangKey(lng_month1 + month - 1)), lt_day, QString::number(day)) : qsl("MONTH_ERR");
}

inline LangString langDayOfWeek(const QDate &date) {
	int32 day = date.dayOfWeek();
	return (day > 0 && day <= 7) ? lang(LangKey(lng_weekday1 + day - 1)) : qsl("DAY_ERR");
}

inline LangString langDayOfWeekFull(const QDate &date) {
	int32 day = date.dayOfWeek();
	return (day > 0 && day <= 7) ? lang(LangKey(lng_weekday1_full + day - 1)) : qsl("DAY_ERR");
}

Qt::LayoutDirection langDir();

class LangLoader {
public:
	const QString &errors() const;
	const QString &warnings() const;

protected:
	LangLoader() : _checked(false) {
		memset(_found, 0, sizeof(_found));
	}

	ushort tagIndex(const QByteArray &tag) const;
	LangKey keyIndex(const QByteArray &key) const;
	bool tagReplaced(LangKey key, ushort tag) const;
	LangKey subkeyIndex(LangKey key, ushort tag, ushort index) const;

	bool feedKeyValue(LangKey key, const QString &value);

	void error(const QString &text) {
		_err.push_back(text);
	}
	void warning(const QString &text) {
		_warn.push_back(text);
	}

private:
	mutable QStringList _err, _warn;
	mutable QString _errors, _warnings;
	mutable bool _checked;
	bool _found[lngkeys_cnt];

	LangLoader(const LangLoader &);
	LangLoader &operator=(const LangLoader &);
};
