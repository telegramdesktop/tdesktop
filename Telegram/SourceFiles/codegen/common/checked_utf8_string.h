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

#include <QtCore/QString>

class QByteArray;

namespace codegen {
namespace common {

class ConstUtf8String;

// Parses a char sequence to a QString using UTF-8 codec.
// You can check for invalid UTF-8 sequence by isValid() method.
class CheckedUtf8String {
public:
	CheckedUtf8String(const CheckedUtf8String &other) = default;
	CheckedUtf8String &operator=(const CheckedUtf8String &other) = default;

	explicit CheckedUtf8String(const char *string, int size = -1);
	explicit CheckedUtf8String(const QByteArray &string);
	explicit CheckedUtf8String(const ConstUtf8String &string);

	bool isValid() const {
		return valid_;
	}
	const QString &toString() const {
		return string_;
	}

private:
	QString string_;
	bool valid_ = true;

};

} // namespace common
} // namespace codegen
