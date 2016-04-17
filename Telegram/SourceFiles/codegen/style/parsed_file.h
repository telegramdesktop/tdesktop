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
#pragma once

#include <memory>
#include <QtCore/QString>
#include <QtCore/QFileInfo>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/style/options.h"
#include "codegen/style/structure.h"

namespace codegen {
namespace style {

// Parses an input file to the internal struct.
class ParsedFile {
public:
	ParsedFile(const Options &options);
	ParsedFile(const ParsedFile &other) = delete;
	ParsedFile &operator=(const ParsedFile &other) = delete;

	struct Token {
		enum class Type {
			Invalid,

			Using,             // value: file path

			DefineStructStart, // value: struct name
			DefineStructField, // value: struct field name
			StructFieldType,   // value: struct field type name
			DefineStructEnd,

			DefineVariable,    // value: variable name
			StructStart,       // value: struct name
			StructParent,      // value: variable parent name
			VariableField,     // value: variable field name
			StructEnd,

			Int,
			Double,
			Pixels,
			String,
			Color,
			Point,
			Sprite,
			Size,
			Transition,
			Cursor,
			Align,
			Margins,
			Font,
		};
		Type type;
		QString value;

		explicit operator bool() const {
			return type != Type::Invalid;
		}
	};

	bool read();

	const structure::Module &data() const {
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
	common::LogStream logErrorUnexpectedToken(const std::string &expected = std::string()) {
		failed_ = true;
		return file_.logErrorUnexpectedToken(expected);
	}

	Token readToken();

	// State value defines what are we waiting next.
	enum class State {
		Default, // [ using | struct name | variable name | end ]
		StructStarted, // [ struct field name | struct end ]
		StructFieldName, // [ struct field type ]
		Variable, // [ struct name | variable value ]
		VariableParents, // [ variable parent name | variable start ]
		VariableStarted, // [ variable field name | variable end]
		VariableChild, // [ variable child value ]
	};

	// Helper methods for readToken() being in specific State.
	Token readInDefault();
	Token readInStructStarted();
	Token readInStructFieldName();
	Token readInVariable();
	Token readInVariableParents();
	Token readInVariableStarted();
	Token readInVariableChild();

	Token readNumericToken();
	Token readValueToken();
	Token readColorToken();
	Token readPointToken();
	Token readSpriteToken();
	Token readSizeToken();
	Token readTransitionToken();
	Token readCursorToken();
	Token readAlignToken();
	Token readMarginsToken();
	Token readFontToken();

	common::BasicTokenizedFile file_;
	Options options_;

	bool failed_ = false;
	State state_ = State::Default;

	structure::Module result_;

};

} // namespace style
} // namespace codegen
