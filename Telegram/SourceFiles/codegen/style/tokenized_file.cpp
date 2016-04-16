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
#include "codegen/style/tokenized_file.h"

#include <iostream>
#include <QtCore/QDir>

#include "codegen/common/basic_tokenized_file.h"
#include "codegen/common/logging.h"

using Token = codegen::style::TokenizedFile::Token;
using Type = Token::Type;
using BasicToken = codegen::common::BasicTokenizedFile::Token;
using BasicType = BasicToken::Type;

namespace codegen {
namespace style {
namespace {

QString plainValue(const BasicToken &token) {
	return token.original.toStringUnchecked();
}

Token invalidToken() {
	return { Type::Invalid, QString() };
}

} // namespace

TokenizedFile::TokenizedFile(const QString &filepath) : file_(filepath) {
}

bool TokenizedFile::putBack() {
	if (currentToken_ > 0) {
		--currentToken_;
		return true;
	}
	return false;
}

Token TokenizedFile::getToken() {
	if (currentToken_ >= tokens_.size()) {
		if (readToken() == Type::Invalid) {
			return invalidToken();
		}
	}
	return tokens_.at(currentToken_++);
}

Type TokenizedFile::readToken() {
	switch (state_) {
	case State::Default: return readInDefault();
	case State::StructStarted: return readInStructStarted();
	case State::StructFieldName: return readInStructFieldName();
	case State::Variable: return readInVariable();
	case State::VariableParents: return readInVariableParents();
	case State::VariableStarted: return readInVariableStarted();
	case State::VariableChild: return readInVariableChild();
	}
	return Type::Invalid;
}

Type TokenizedFile::readInDefault() {
	if (auto basicToken = file_.getToken(BasicType::Name)) {
		if (plainValue(basicToken) == "using") {
			if (auto usingFile = file_.getToken(BasicType::String)) {
				if (file_.getToken(BasicType::Semicolon)) {
					return saveToken(Type::Using, usingFile.value);
				}
				file_.logErrorUnexpectedToken("';'");
			} else {
				file_.logErrorUnexpectedToken("file path");
			}
			return Type::Invalid;
		}
		if (auto braceToken = file_.getToken(BasicType::LeftBrace)) {
			state_ = State::StructStarted;
			return saveToken(Type::DefineStruct, plainValue(basicToken));
		} else if (auto colonToken = file_.getToken(BasicType::Colon)) {
			state_ = State::Variable;
			return saveToken(Type::DefineVariable, plainValue(basicToken));
		}
		file_.logErrorUnexpectedToken("using keyword, or struct definition, or variable definition");
	}
	return Type::Invalid;
}

Type TokenizedFile::readInStructStarted() {
	return Type::Invalid;
}

Type TokenizedFile::readInStructFieldName() {
	return Type::Invalid;
}

Type TokenizedFile::readInVariable() {
	return Type::Invalid;
}

Type TokenizedFile::readInVariableParents() {
	return Type::Invalid;
}

Type TokenizedFile::readInVariableStarted() {
	return Type::Invalid;
}

Type TokenizedFile::readInVariableChild() {
	return Type::Invalid;
}

Type TokenizedFile::saveToken(Type type, const QString &value) {
	tokens_.push_back({ type, value });
	return type;
}

} // namespace style
} // namespace codegen
