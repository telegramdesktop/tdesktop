/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	QVector<QByteArray> singleLineComments() const;

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

	QVector<QByteArray> singleLineComments_;

};

} // namespace common
} // namespace codegen
