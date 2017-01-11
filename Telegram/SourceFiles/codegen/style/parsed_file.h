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
#include <functional>
#include <QImage>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/style/options.h"
#include "codegen/style/module.h"

namespace codegen {
namespace style {

using Modifier = std::function<void(QImage &png100x, QImage &png200x)>;
Modifier GetModifier(const QString &name);

// Parses an input file to the internal struct.
class ParsedFile {
public:
	explicit ParsedFile(const Options &options);
	ParsedFile(const ParsedFile &other) = delete;
	ParsedFile &operator=(const ParsedFile &other) = delete;

	bool read();

	using ModulePtr = std::unique_ptr<structure::Module>;
	ModulePtr getResult() {
		return std::move(module_);
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
	common::LogStream logErrorTypeMismatch();
	common::LogStream logAssert(bool assertion) {
		if (!assertion) {
			return logError(common::kErrorInternal) << "internal - ";
		}
		return common::LogStream(common::LogStream::Null);
	}

	// Helper methods for context-dependent reading.
	ModulePtr readIncluded();
	structure::Struct readStruct(const QString &name);
	structure::Variable readVariable(const QString &name);

	structure::StructField readStructField(const QString &name);
	structure::Type readType();
	structure::Value readValue();

	structure::Value readStructValue();
	structure::Value defaultConstructedStruct(const structure::FullName &name);
	void applyStructParent(structure::Value &result, const structure::FullName &parentName);
	bool readStructValueInner(structure::Value &result);
	bool assignStructField(structure::Value &result, const structure::Variable &field);
	bool readStructParents(structure::Value &result);

	// Simple methods for reading value types.
	structure::Value readPositiveValue();
	structure::Value readNumericValue();
	structure::Value readStringValue();
	structure::Value readColorValue();
	structure::Value readPointValue();
	structure::Value readSizeValue();
	structure::Value readAlignValue();
	structure::Value readMarginsValue();
	structure::Value readFontValue();
	structure::Value readIconValue();
	structure::Value readCopyValue();

	structure::Value readNumericOrNumericCopyValue();
	structure::Value readStringOrStringCopyValue();

	structure::data::monoicon readMonoIconFields();
	QString readMonoIconFilename();

	// Read next token and fire unexpected token error if it is not of "type".
	using BasicToken = common::BasicTokenizedFile::Token;
	BasicToken assertNextToken(BasicToken::Type type);

	// Look through include directories in options_ and find absolute include path.
	Options includedOptions(const QString &filepath);

	// Compose context-dependent full name.
	structure::FullName composeFullName(const QString &name);

	QString filePath_;
	common::BasicTokenizedFile file_;
	Options options_;
	bool failed_ = false;
	ModulePtr module_;

	QMap<std::string, structure::Type> typeNames_ = {
		{ "int"       , { structure::TypeTag::Int } },
		{ "double"    , { structure::TypeTag::Double } },
		{ "pixels"    , { structure::TypeTag::Pixels } },
		{ "string"    , { structure::TypeTag::String } },
		{ "color"     , { structure::TypeTag::Color } },
		{ "point"     , { structure::TypeTag::Point } },
		{ "size"      , { structure::TypeTag::Size } },
		{ "align"     , { structure::TypeTag::Align } },
		{ "margins"   , { structure::TypeTag::Margins } },
		{ "font"      , { structure::TypeTag::Font } },
		{ "icon"      , { structure::TypeTag::Icon } },
	};

};

} // namespace style
} // namespace codegen
