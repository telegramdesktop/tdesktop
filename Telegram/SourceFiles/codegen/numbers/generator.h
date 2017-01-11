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

#include <memory>
#include <QtCore/QString>
#include <QtCore/QSet>
#include "codegen/common/cpp_file.h"
#include "codegen/numbers/parsed_file.h"

namespace codegen {
namespace numbers {

class Generator {
public:
	Generator(const Rules &rules, const QString &destBasePath, const common::ProjectInfo &project);
	Generator(const Generator &other) = delete;
	Generator &operator=(const Generator &other) = delete;

	bool writeHeader();
	bool writeSource();

private:
	const Rules &rules_;
	QString basePath_;
	const common::ProjectInfo &project_;
	std::unique_ptr<common::CppFile> source_, header_;

};

} // namespace numbers
} // namespace codegen
