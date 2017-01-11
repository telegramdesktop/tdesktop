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
#include <iostream>

namespace codegen {
namespace common {

// Common error codes.
constexpr int kErrorFileNotFound        = 101;
constexpr int kErrorFileTooLarge        = 102;
constexpr int kErrorFileNotOpened       = 103;
constexpr int kErrorUnexpectedEndOfFile = 104;

// Wrapper around std::ostream that adds '\n' to the end of the logging line.
class LogStream {
public:
	enum NullType {
		Null,
	};
	explicit LogStream(NullType) : final_(false) {
	}
	explicit LogStream(std::ostream &stream) : stream_(&stream) {
	}
	LogStream(LogStream &&other) : stream_(other.stream_), final_(other.final_) {
		other.final_ = false;
	}
	std::ostream *stream() const {
		return stream_;
	}
	~LogStream() {
		if (final_) {
			*stream_ << '\n';
		}
	}

private:
	std::ostream *stream_ = nullptr;
	bool final_ = true;

};

template <typename T>
LogStream operator<<(LogStream &&stream, T &&value) {
	if (auto ostream = stream.stream()) {
		*ostream << std::forward<T>(value);
	}
	return std::forward<LogStream>(stream);
}

// Outputs file name, line number and error code to std::err. Usage:
// logError(kErrorFileTooLarge, filepath) << "file too large, size=" << size;
LogStream logError(int code, const QString &filepath, int line = 0);

void logSetWorkingPath(const QString &workingpath);

static constexpr int kErrorInternal = 666;

} // namespace common
} // namespace codegen
