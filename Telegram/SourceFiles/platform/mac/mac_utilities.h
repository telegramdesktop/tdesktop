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

#include "lang/lang_keys.h"

#include <Cocoa/Cocoa.h>

namespace Platform {

inline NSString *Q2NSString(const QString &str) {
	return [NSString stringWithUTF8String:str.toUtf8().constData()];
}

inline NSString *NSlang(LangKey key) {
	return Q2NSString(lang(key));
}

inline QString NS2QString(NSString *str) {
	return QString::fromUtf8([str cStringUsingEncoding:NSUTF8StringEncoding]);
}


template <int Size>
inline QString MakeFromLetters(const uint32 (&letters)[Size]) {
	QString result;
	result.reserve(Size);
	for (int32 i = 0; i < Size; ++i) {
		auto code = letters[i];
		auto salt1 = (code >> 8) & 0xFFU;
		auto salt2 = (code >> 24) & 0xFFU;
		auto part1 = ((code & 0xFFU) ^ (salt1 ^ salt2)) & 0xFFU;
		auto part2 = (((code >> 16) & 0xFFU) ^ (salt1 ^ ~salt2)) & 0xFFU;
		result.push_back(QChar((part2 << 8) | part1));
	}
	return result;
}

} // namespace Platform
