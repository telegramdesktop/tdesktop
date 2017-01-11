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

#include "codegen/common/clean_file.h"

namespace codegen {
namespace common {

// Wrapper allows you to read forward the CleanFile without overflow checks.
class CleanFileReader {
public:
	explicit CleanFileReader(const QString &filepath) : file_(filepath) {
	}
	explicit CleanFileReader(const QByteArray &content, const QString &filepath = QString()) : file_(content, filepath) {
	}

	bool read() {
		if (!file_.read()) {
			return false;
		}
		pos_ = file_.data();
		end_ = file_.end();
		return true;
	}
	bool atEnd() const {
		return (pos_ == end_);
	}
	char currentChar() const {
		return atEnd() ? 0 : *pos_;
	}
	bool skipChar() {
		if (atEnd()) {
			return false;
		}
		++pos_;
		return true;
	}
	const char *currentPtr() const {
		return pos_;
	}
	int charsLeft() const {
		return (end_ - pos_);
	}

	// Log error to std::cerr with 'code' at line number 'line' in data().
	LogStream logError(int code, int line) const {
		return std::forward<LogStream>(file_.logError(code, line));
	}


private:
	CleanFile file_;
	const char *pos_ = nullptr;
	const char *end_ = nullptr;

};

} // namespace common
} // namespace codegen
