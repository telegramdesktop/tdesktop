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
#pragma once

namespace Lang {

constexpr auto kTagReplacementSize = 4;

int FindTagReplacementPosition(const QString &original, ushort tag);

struct PluralResult {
	QString string;
	QString replacement;
};
PluralResult Plural(ushort keyBase, float64 value);
void UpdatePluralRules(const QString &languageId);

template <typename ResultString>
struct StartReplacements;

template <>
struct StartReplacements<QString> {
	static inline QString Call(QString &&langString) {
		return std::move(langString);
	}
};

template <typename ResultString>
struct ReplaceTag;

template <>
struct ReplaceTag<QString> {
	static inline QString Call(QString &&original, ushort tag, const QString &replacement) {
		auto replacementPosition = FindTagReplacementPosition(original, tag);
		if (replacementPosition < 0) {
			return std::move(original);
		}
		return Replace(std::move(original), replacement, replacementPosition);
	}
	static QString Replace(QString &&original, const QString &replacement, int start);

};

} // namespace Lang
