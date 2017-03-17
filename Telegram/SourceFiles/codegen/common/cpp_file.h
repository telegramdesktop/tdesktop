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
#include <QtCore/QVector>
#include <QtCore/QTextStream>

namespace codegen {
namespace common {

struct ProjectInfo {
	QString name;
	QString source;
	bool forceReGenerate;
};

// Creates a file with license header and codegen warning.
class CppFile {
public:
	// If "basepath" is empty the folder containing "path" will be chosen.
	// File ending with .cpp will be treated as source, otherwise like header.
	CppFile(const QString &path, const ProjectInfo &project);

	QTextStream &stream() {
		return stream_;
	}

	CppFile &newline() {
		stream() << "\n";
		return *this;
	}
	CppFile &include(const QString &header);

	// Empty name adds anonymous namespace.
	CppFile &pushNamespace(const QString &name = QString());
	CppFile &popNamespace();

	bool finalize();

private:
	QString filepath_;
	QByteArray content_;
	QTextStream stream_;
	QVector<QString> namespaces_;
	bool forceReGenerate_;

};

} // namespace common
} // namespace codegen
