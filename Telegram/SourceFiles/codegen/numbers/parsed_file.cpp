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
#include "codegen/numbers/parsed_file.h"

#include <iostream>
#include <QtCore/QMap>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/common/logging.h"

using BasicToken = codegen::common::BasicTokenizedFile::Token;
using BasicType = BasicToken::Type;

namespace codegen {
namespace numbers {
namespace {

} // namespace

ParsedFile::ParsedFile(const Options &options)
: file_(options.inputPath)
, options_(options) {
}

bool ParsedFile::read() {
	if (!file_.read()) {
		return false;
	}

	auto filepath = QFileInfo(options_.inputPath).absoluteFilePath();
	do {
		if (auto startToken = file_.getToken(BasicType::Name)) {
		}
		if (file_.atEnd()) {
			break;
		}
		logErrorUnexpectedToken() << "numbers rule";
	} while (!failed());

	if (failed()) {
		result_.data.clear();
	}
	return !failed();
}

} // namespace numbers
} // namespace codegen
