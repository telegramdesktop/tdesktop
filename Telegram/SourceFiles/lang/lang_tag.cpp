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
#include "lang/lang_tag.h"

#include "lang/lang_keys.h"

namespace Lang {

QString Tag(const QString &original, ushort tag, const QString &replacement) {
	for (auto s = original.constData(), ch = s, e = ch + original.size(); ch != e;) {
		if (*ch == TextCommand) {
			if (ch + 3 < e && (ch + 1)->unicode() == TextCommandLangTag && *(ch + 3) == TextCommand) {
				if ((ch + 2)->unicode() == 0x0020 + tag) {
					auto result = QString();
					result.reserve(original.size() + replacement.size() - 4);
					if (ch > s) result.append(original.midRef(0, ch - s));
					result.append(replacement);
					if (ch + 4 < e) result.append(original.midRef(ch - s + 4));
					return result;
				} else {
					ch += 4;
				}
			} else {
				auto next = textSkipCommand(ch, e);
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
	return original;
}

QString Plural(ushort key0, ushort tag, float64 value) { // current lang dependent
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
		auto v = lang(LangKey(key));
		if (!v.isEmpty()) {
			return Tag(v, tag, sv);
		}
		--key;
	}
	return Tag(lang(LangKey(key0)), tag, sv);
}

} // namespace Lang
