/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_file.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include <gsl/gsl_util>

namespace Export {
namespace Output {

File::File(const QString &path) : _path(path) {
}

int File::size() const {
	return _offset;
}

bool File::empty() const {
	return !_offset;
}

File::Result File::writeBlock(const QByteArray &block) {
	const auto result = writeBlockAttempt(block);
	if (result != Result::Success) {
		_file.clear();
	}
	return result;
}

File::Result File::writeBlockAttempt(const QByteArray &block) {
	if (const auto result = reopen(); result != Result::Success) {
		return result;
	}
	return (_file->write(block) == block.size() && _file->flush())
		? Result::Success
		: Result::Error;
}

File::Result File::reopen() {
	if (_file && _file->isOpen()) {
		return Result::Success;
	}
	_file.emplace(_path);
	if (_file->exists()) {
		if (_file->size() < _offset) {
			return Result::FatalError;
		} else if (!_file->resize(_offset)) {
			return Result::Error;
		}
	} else if (_offset > 0) {
		return Result::FatalError;
	}
	if (_file->open(QIODevice::Append)) {
		return Result::Success;
	}
	const auto info = QFileInfo(_path);
	const auto dir = info.absoluteDir();
	return (!dir.exists()
		&& dir.mkpath(dir.absolutePath())
		&& _file->open(QIODevice::Append))
		? Result::Success
		: Result::Error;
}

QString File::PrepareRelativePath(
		const QString &folder,
		const QString &suggested) {
	if (!QFile::exists(folder + suggested)) {
		return suggested;
	}

	// Not lastIndexOf('.') so that "file.tar.xz" won't be messed up.
	const auto position = suggested.indexOf('.');
	const auto base = suggested.midRef(0, position);
	const auto extension = (position >= 0)
		? suggested.midRef(position)
		: QStringRef();
	const auto relativePart = [&](int attempt) {
		auto result = QString(" (%1)").arg(attempt);
		result.prepend(base);
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

} // namespace Output
} // namespace File
