/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/common/logging.h"

#include <iostream>
#include <QtCore/QDir>

namespace codegen {
namespace common {
namespace {

QString WorkingPath = ".";

std::string relativeLocalPath(const QString &filepath) {
	auto name = QFile::encodeName(QDir(WorkingPath).relativeFilePath(filepath));
	return name.constData();
}

} // namespace

LogStream logError(int code, const QString &filepath, int line) {
	std::cerr << relativeLocalPath(filepath);
	if (line > 0) {
		std::cerr << '(' << line << ')';
	}
	std::cerr << ": error " << code << ": ";
	return LogStream(std::cerr);
}

void logSetWorkingPath(const QString &workingpath) {
	WorkingPath = workingpath;
}

} // namespace common
} // namespace codegen