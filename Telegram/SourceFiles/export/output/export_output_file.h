/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/optional.h"

#include <QtCore/QFile>
#include <QtCore/QString>
#include <QtCore/QByteArray>

namespace Export {
namespace Output {

class File {
public:
	File(const QString &path);

	enum class Result {
		Success,
		Error,
		FatalError,
	};
	Result writeBlock(const QByteArray &block);

private:
	Result reopen();
	Result writeBlockAttempt(const QByteArray &block);

	QString _path;
	int _offset = 0;
	base::optional<QFile> _file;

};

} // namespace Output
} // namespace File
