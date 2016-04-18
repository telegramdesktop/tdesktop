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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "codegen/numbers/generator.h"

#include <QtCore/QDir>
#include <QtCore/QSet>
#include <functional>

namespace codegen {
namespace numbers {
namespace {

} // namespace

Generator::Generator(const Rules &rules, const QString &destBasePath, const common::ProjectInfo &project)
: rules_(rules)
, basePath_(destBasePath)
, project_(project) {
}

bool Generator::writeHeader() {
	header_ = std::make_unique<common::CppFile>(basePath_ + ".h", project_);

	header_->stream() << "QVector<int> phoneNumberParse(const QString &number);\n";

	return header_->finalize();
}

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(basePath_ + ".cpp", project_);

	return source_->finalize();
}

} // namespace numbers
} // namespace codegen
