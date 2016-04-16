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

namespace codegen {
namespace style {

// Parses a file as a list of tokens.
class TokenizedFile {
public:
	TokenizedFile(const QString &filepath);
	TokenizedFile(const TokenizedFile &other) = delete;
	TokenizedFile &operator=(const TokenizedFile &other) = delete;

	using ConstUtf8String = common::ConstUtf8String;
	struct Token {
		enum class Type {
			Invalid,

			Using,

			DefineStruct,
			DefineField,
			FieldType,

			DefineVariable,
			Struct,
			StructParent,

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

	bool read() {
		return file_.read();
	}
	bool atEnd() const {
		return file_.atEnd();
	}

	Token getToken();
	bool putBack();
	bool failed() const {
		return file_.failed();
	}

	// Log error to std::cerr with 'code' at the current position in file.
	common::LogStream logError(int code) const {
		return file_.logError(code);
	}

private:
	using Type = Token::Type;
	Type readToken();

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
	Type readInDefault();
	Type readInStructStarted();
	Type readInStructFieldName();
	Type readInVariable();
	Type readInVariableParents();
	Type readInVariableStarted();
	Type readInVariableChild();

	Type saveToken(Type type, const QString &value = QString());

	common::BasicTokenizedFile file_;
	QList<Token> tokens_;
	int currentToken_ = 0;
	State state_ = State::Default;

};

} // namespace style
} // namespace codegen
