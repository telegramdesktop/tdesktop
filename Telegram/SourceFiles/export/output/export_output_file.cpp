/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_file.h"

#include <gsl/gsl_util>

namespace Export {
namespace Output {

File::File(const QString &path) : _path(path) {
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
	return _file->open(QIODevice::Append)
		? Result::Success
		: Result::Error;
}

} // namespace Output
} // namespace File
