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
#include "codegen/style/parsed_file.h"

#include <iostream>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/common/logging.h"

using Token = codegen::style::ParsedFile::Token;
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

ParsedFile::ParsedFile(const Options &options)
: file_(options.inputPath)
, options_(options) {
}

bool ParsedFile::read() {
	if (!file_.read()) {
		return false;
	}

	for (auto token = readInDefault(); token; token = readInDefault()) {
		if (token.type == Type::Using) {
			auto includedOptions = options_;
//			includedOptions.inputPath = findIncludePath(token.value);
			ParsedFile included(includedOptions);
			if (!included.read()) {
				return false;
			}
			result_.includes.push_back(included.data());
		//} else if (token.type == Type::DefineStructStart) {
		//	if (!read)
		}
	}
	return !failed();
}

Token ParsedFile::readToken() {
	switch (state_) {
	case State::Default: return readInDefault();
	case State::StructStarted: return readInStructStarted();
	case State::StructFieldName: return readInStructFieldName();
	case State::Variable: return readInVariable();
	case State::VariableParents: return readInVariableParents();
	case State::VariableStarted: return readInVariableStarted();
	case State::VariableChild: return readInVariableChild();
	}
	return invalidToken();
}

Token ParsedFile::readInDefault() {
	if (auto startToken = file_.getToken(BasicType::Name)) {
		if (plainValue(startToken) == "using") {
			if (auto usingFile = file_.getToken(BasicType::String)) {
				if (file_.getToken(BasicType::Semicolon)) {
					return { Type::Using, usingFile.value };
				}
				logErrorUnexpectedToken("';'");
			} else {
				logErrorUnexpectedToken("file path");
			}
			return invalidToken();
		} else if (auto braceOpen = file_.getToken(BasicType::LeftBrace)) {
			state_ = State::StructStarted;
			return { Type::DefineStructStart, plainValue(startToken) };
		} else if (auto colonToken = file_.getToken(BasicType::Colon)) {
			state_ = State::Variable;
			return { Type::DefineVariable, plainValue(startToken) };
		}
	}
	if (!file_.atEnd()) {
		logErrorUnexpectedToken("using keyword, or struct definition, or variable definition");
	}
	return invalidToken();
}

Token ParsedFile::readInStructStarted() {
	if (auto fieldName = file_.getToken(BasicType::Name)) {
		state_ = State::StructFieldName;
		return { Type::DefineStructField, plainValue(fieldName) };
	} else if (auto braceClose = file_.getToken(BasicType::RightBrace)) {
		state_ = State::Default;
		return { Type::DefineStructEnd };
	}
	logErrorUnexpectedToken("struct field name or '}'");
	return invalidToken();
}

Token ParsedFile::readInStructFieldName() {
	if (auto colonToken = file_.getToken(BasicType::Colon)) {
		if (auto fieldType = file_.getToken(BasicType::Name)) {
			if (file_.getToken(BasicType::Semicolon)) {
				state_ = State::StructStarted;
				return { Type::StructFieldType, plainValue(fieldType) };
			}
			logErrorUnexpectedToken(";");
		} else {
			logErrorUnexpectedToken("struct field type name");
		}
	} else {
		logErrorUnexpectedToken("':'");
	}
	return invalidToken();
}

Token ParsedFile::readInVariable() {
	if (auto value = readValueToken()) {
		if (file_.getToken(BasicType::Semicolon)) {
			state_ = State::Default;
			return value;
		}
		logErrorUnexpectedToken(";");
		return invalidToken();
	}
	if (failed()) {
		return invalidToken();
	}

	if (auto structName = file_.getToken(BasicType::Name)) {
		if (file_.getToken(BasicType::LeftParenthesis)) {
			state_ = State::VariableParents;
			return { Type::StructStart, plainValue(structName) };
		} else if (file_.getToken(BasicType::LeftBrace)) {
			state_ = State::VariableStarted;
			return { Type::StructStart, plainValue(structName) };
		} else {
			logErrorUnexpectedToken("'(' or '{'");
		}
	} else {
		logErrorUnexpectedToken("variable value");
	}
	return invalidToken();
}

Token ParsedFile::readInVariableParents() {
	if (auto parentName = file_.getToken(BasicType::Name)) {
		if (file_.getToken(BasicType::Comma)) {
			return { Type::StructParent, plainValue(parentName) };
		} else if (file_.getToken(BasicType::RightParenthesis)) {
			if (file_.getToken(BasicType::LeftBrace)) {
				state_ = State::VariableStarted;
				return { Type::StructParent, plainValue(parentName) };
			}
			logErrorUnexpectedToken("'{'");
		} else {
			logErrorUnexpectedToken("',' or ')'");
		}
	} else {
		logErrorUnexpectedToken("struct variable parent");
	}
	return invalidToken();
}

Token ParsedFile::readInVariableStarted() {
	if (auto fieldName = file_.getToken(BasicType::Name)) {
		state_ = State::VariableChild;
		return { Type::VariableField, plainValue(fieldName) };
	} else if (auto braceClose = file_.getToken(BasicType::RightBrace)) {
		state_ = State::Default;
		return { Type::StructEnd };
	}
	logErrorUnexpectedToken("struct variable field name or '}'");
	return invalidToken();
}

Token ParsedFile::readInVariableChild() {
	if (auto value = readValueToken()) {
		if (file_.getToken(BasicType::Semicolon)) {
			state_ = State::Default;
			return value;
		}
		logErrorUnexpectedToken(";");
	} else {
		logErrorUnexpectedToken("variable field value");
	}
	return invalidToken();
}

Token ParsedFile::readNumericToken() {
	auto numericToken = file_.getAnyToken();
	if (numericToken.type == BasicType::Int) {
		return { Type::Int, plainValue(numericToken) };
	} else if (numericToken.type == BasicType::Double) {
		return { Type::Double, plainValue(numericToken) };
	} else if (numericToken.type == BasicType::Name) {
		auto value = plainValue(numericToken);
		auto match = QRegularExpression("^\\d+px$").match(value);
		if (match.hasMatch()) {
			return { Type::Pixels, value.mid(0, value.size() - 2) };
		}
	}
	file_.putBack();
	return invalidToken();
}

Token ParsedFile::readValueToken() {
	if (auto colorValue = readColorToken()) {
		return colorValue;
	} else if (auto pointValue = readPointToken()) {
		return pointValue;
	} else if (auto spriteValue = readSpriteToken()) {
		return spriteValue;
	} else if (auto sizeValue = readSizeToken()) {
		return sizeValue;
	} else if (auto transitionValue = readTransitionToken()) {
		return transitionValue;
	} else if (auto cursorValue = readCursorToken()) {
		return cursorValue;
	} else if (auto alignValue = readAlignToken()) {
		return alignValue;
	} else if (auto marginsValue = readMarginsToken()) {
		return marginsValue;
	} else if (auto fontValue = readFontToken()) {
		return fontValue;
	} else if (auto numericValue = readNumericToken()) {
		return numericValue;
	} else if (auto stringValue = file_.getToken(BasicType::String)) {
		return { Type::String, stringValue.value };
	} else if (auto minusToken = file_.getToken(BasicType::Minus)) {
		if (auto positiveValue = readNumericToken()) {
			return { positiveValue.type, '-' + positiveValue.value };
		}
		logErrorUnexpectedToken("numeric value");
	}
	return invalidToken();
}

Token ParsedFile::readColorToken() {
	return invalidToken();
}

Token ParsedFile::readPointToken() {
	return invalidToken();
}

Token ParsedFile::readSpriteToken() {
	return invalidToken();
}

Token ParsedFile::readSizeToken() {
	return invalidToken();
}

Token ParsedFile::readTransitionToken() {
	return invalidToken();
}

Token ParsedFile::readCursorToken() {
	return invalidToken();
}

Token ParsedFile::readAlignToken() {
	return invalidToken();
}

Token ParsedFile::readMarginsToken() {
	return invalidToken();
}

Token ParsedFile::readFontToken() {
	return invalidToken();
}

} // namespace style
} // namespace codegen
