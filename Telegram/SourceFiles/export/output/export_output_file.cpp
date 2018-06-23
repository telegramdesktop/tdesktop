/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_file.h"

#include "export/output/export_output_result.h"
#include "export/output/export_output_stats.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include <gsl/gsl_util>

namespace Export {
namespace Output {

File::File(const QString &path, Stats *stats) : _path(path), _stats(stats) {
}

int File::size() const {
	return _offset;
}

bool File::empty() const {
	return !_offset;
}

Result File::writeBlock(const QByteArray &block) {
	const auto result = writeBlockAttempt(block);
	if (!result) {
		_file.clear();
	}
	return result;
}

Result File::writeBlockAttempt(const QByteArray &block) {
	if (_stats && !_inStats) {
		_inStats = true;
		_stats->incrementFiles();
	}
	if (const auto result = reopen(); !result) {
		return result;
	}
	const auto size = block.size();
	if (!size) {
		return Result::Success();
	}
	if (_file->write(block) == size && _file->flush()) {
		_offset += size;
		if (_stats) {
			_stats->incrementBytes(size);
		}
		return Result::Success();
	}
	return error();
}

Result File::reopen() {
	if (_file && _file->isOpen()) {
		return Result::Success();
	}
	_file.emplace(_path);
	if (_file->exists()) {
		if (_file->size() < _offset) {
			return fatalError();
		} else if (!_file->resize(_offset)) {
			return error();
		}
	} else if (_offset > 0) {
		return fatalError();
	}
	if (_file->open(QIODevice::Append)) {
		return Result::Success();
	}
	const auto info = QFileInfo(_path);
	const auto dir = info.absoluteDir();
	return (!dir.exists()
		&& dir.mkpath(dir.absolutePath())
		&& _file->open(QIODevice::Append))
		? Result::Success()
		: error();
}

Result File::error() const {
	return Result(Result::Type::Error, _path);
}

Result File::fatalError() const {
	return Result(Result::Type::FatalError, _path);
}

QString File::PrepareRelativePath(
		const QString &folder,
		const QString &suggested) {
	if (!QFile::exists(folder + suggested)) {
		return suggested;
	}

	// Not lastIndexOf('.') so that "file.tar.xz" won't be messed up.
	const auto position = suggested.indexOf('.');
	const auto base = suggested.midRef(0, position).toString();
	const auto extension = (position >= 0)
		? suggested.midRef(position)
		: QStringRef();
	const auto relativePart = [&](int attempt) {
		auto result = base + QString(" (%1)").arg(attempt);
		result.append(extension);
		return result;
	};
	auto attempt = 0;
	while (true) {
		const auto relativePath = relativePart(++attempt);
		if (!QFile::exists(folder + relativePath)) {
			return relativePath;
		}
	}
}

Result File::Copy(
		const QString &source,
		const QString &path,
		Stats *stats) {
	QFile f(source);
	if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
		return Result(Result::Type::FatalError, source);
	}
	const auto bytes = f.readAll();
	if (bytes.size() != f.size()) {
		return Result(Result::Type::FatalError, source);
	}
	return File(path, stats).writeBlock(bytes);
}

} // namespace Output
} // namespace File
