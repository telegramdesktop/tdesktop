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
#include <QtCore/QByteArray>
#include <QtCore/QVector>

#include "codegen/common/logging.h"

namespace codegen {
namespace common {

// Reads a file removing all C-style comments.
class CleanFile {
public:
	explicit CleanFile(const QString &filepath);
	explicit CleanFile(const QByteArray &content, const QString &filepath = QString());
	CleanFile(const CleanFile &other) = delete;
	CleanFile &operator=(const CleanFile &other) = delete;

	bool read();

	const char *data() const {
		return result_.constData();
	}
	const char *end() const {
		return result_.constEnd();
	}

	static constexpr int MaxSize = 10 * 1024 * 1024;

	// Log error to std::cerr with 'code' at line number 'line' in data().
	LogStream logError(int code, int line) const;

private:
	QString filepath_;
	QByteArray content_, result_;
	bool read_;
	//struct Comment {
	//	int offset;
	//	QByteArray content;
	//};
	//QVector<Comment> comments_;

};

} // namespace common
} // namespace codegen
