/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
