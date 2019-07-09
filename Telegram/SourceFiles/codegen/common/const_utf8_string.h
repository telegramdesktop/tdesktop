/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <string>
#include <QtCore/QString>
#include <QtCore/QByteArray>

namespace codegen {
namespace common {

// This is a simple wrapper around (const char*, size).
// Not null-terminated! It does not hold any ownership.
class ConstUtf8String {
public:
	explicit ConstUtf8String(const char *string, int size = -1) : string_(string) {
		if (size < 0) {
			size = strlen(string);
		}
		size_ = size;
	}
	ConstUtf8String(const char *string, const char *end) : ConstUtf8String(string, end - string) {
	}

	QByteArray toByteArray() const {
		return QByteArray(string_, size_);
	}
	std::string toStdString() const {
		return std::string(string_, size_);
	}
	QString toStringUnchecked() const {
		return QString::fromUtf8(string_, size_);
	}
	bool empty() const {
		return size_ == 0;
	}
	const char *data() const {
		return string_;
	}
	int size() const {
		return size_;
	}
	const char *end() const {
		return data() + size();
	}
	ConstUtf8String mid(int pos, int size = -1) {
		return ConstUtf8String(string_ + pos, std::max(std::min(size, size_ - pos), 0));
	}

private:
	const char *string_;
	int size_;

};

} // namespace common
} // namespace codegen
