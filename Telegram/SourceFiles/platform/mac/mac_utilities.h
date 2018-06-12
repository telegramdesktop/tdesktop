/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
