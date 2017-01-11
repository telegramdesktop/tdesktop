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
#include <string>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/numbers/options.h"

namespace codegen {
namespace numbers {

using Rule = QVector<int>;
struct Rules {
	QMap<QString, Rule> data;
};

// Parses an input file to the internal struct.
class ParsedFile {
public:
	explicit ParsedFile(const Options &options);
	ParsedFile(const ParsedFile &other) = delete;
	ParsedFile &operator=(const ParsedFile &other) = delete;

	bool read();

	Rules getResult() {
		return result_;
	}

private:

	bool failed() const {
		return failed_ || file_.failed();
	}

	// Log error to std::cerr with 'code' at the current position in file.
	common::LogStream logError(int code) {
		failed_ = true;
		return file_.logError(code);
	}
	common::LogStream logErrorUnexpectedToken() {
		failed_ = true;
		return file_.logErrorUnexpectedToken();
	}

	QByteArray content_;
	common::BasicTokenizedFile file_;
	Options options_;
	bool failed_ = false;
	Rules result_;

};

} // namespace numbers
} // namespace codegen
