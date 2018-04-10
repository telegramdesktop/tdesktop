/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/common/checked_utf8_string.h"

#include <iostream>
#include <QtCore/QTextCodec>

#include "codegen/common/const_utf8_string.h"

namespace codegen {
namespace common {

CheckedUtf8String::CheckedUtf8String(const char *string, int size) {
	if (size < 0) {
		size = strlen(string);
	}
	if (!size) { // Valid empty string
		return;
	}

	QTextCodec::ConverterState state;
	QTextCodec *codec = QTextCodec::codecForName("UTF-8");
	string_ = codec->toUnicode(string, size, &state);
	if (state.invalidChars > 0) {
		valid_ = false;
	}
}

CheckedUtf8String::CheckedUtf8String(const QByteArray &string) : CheckedUtf8String(string.constData(), string.size()) {
}

CheckedUtf8String::CheckedUtf8String(const ConstUtf8String &string) : CheckedUtf8String(string.data(), string.size()) {
}

} // namespace common
} // namespace codegen
