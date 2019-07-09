/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
