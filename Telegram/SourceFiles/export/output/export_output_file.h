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

struct Result;
class Stats;

class File {
public:
	File(const QString &path, Stats *stats);

	[[nodiscard]] int size() const;
	[[nodiscard]] bool empty() const;

	[[nodiscard]] Result writeBlock(const QByteArray &block);

	[[nodiscard]] static QString PrepareRelativePath(
		const QString &folder,
		const QString &suggested);

	[[nodiscard]] static Result Copy(
		const QString &source,
		const QString &path,
		Stats *stats);

private:
	[[nodiscard]] Result reopen();
	[[nodiscard]] Result writeBlockAttempt(const QByteArray &block);

	[[nodiscard]] Result error() const;
	[[nodiscard]] Result fatalError() const;

	QString _path;
	int _offset = 0;
	std::optional<QFile> _file;

	Stats *_stats = nullptr;
	bool _inStats = false;

};

} // namespace Output
} // namespace File
