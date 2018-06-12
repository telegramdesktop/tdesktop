/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	QVector<QByteArray> singleLineComments() const {
		return file_.singleLineComments();
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
