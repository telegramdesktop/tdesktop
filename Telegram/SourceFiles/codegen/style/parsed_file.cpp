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
#include <QtCore/QMap>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/common/logging.h"

using BasicToken = codegen::common::BasicTokenizedFile::Token;
using BasicType = BasicToken::Type;

namespace codegen {
namespace style {
namespace {

constexpr int kErrorInIncluded         = 801;
constexpr int kErrorTypeMismatch       = 802;
constexpr int kErrorUnknownField       = 803;
constexpr int kErrorIdentifierNotFound = 804;

QString tokenValue(const BasicToken &token) {
	if (token.type == BasicType::String) {
		return token.value;
	}
	return token.original.toStringUnchecked();
}

bool isValidColor(const QString &str) {
	auto len = str.size();
	if (len != 3 && len != 4 && len != 6 && len != 8) {
		return false;
	}

	for (auto ch : str) {
		auto code = ch.unicode();
		if ((code < '0' || code > '9') && (code < 'a' || code > 'f') && (code < 'A' || code > 'F')) {
			return false;
		}
	}
	return true;
}

std::string logFullName(const structure::FullName &name) {
	return name.join('.').toStdString();
}

std::string logType(const structure::Type &type) {
	if (type.tag == structure::TypeTag::Struct) {
		return "struct " + logFullName(type.name);
	}
	static auto builtInTypes = new QMap<structure::TypeTag, std::string> {
		{ structure::TypeTag::Int       , "int" },
		{ structure::TypeTag::Double    , "double" },
		{ structure::TypeTag::Pixels    , "pixels" },
		{ structure::TypeTag::String    , "string" },
		{ structure::TypeTag::Color     , "color" },
		{ structure::TypeTag::Point     , "point" },
		{ structure::TypeTag::Sprite    , "sprite" },
		{ structure::TypeTag::Size      , "size" },
		{ structure::TypeTag::Transition, "transition" },
		{ structure::TypeTag::Cursor    , "cursor" },
		{ structure::TypeTag::Align     , "align" },
		{ structure::TypeTag::Margins   , "margins" },
		{ structure::TypeTag::Font      , "font" },
	};
	return builtInTypes->value(type.tag, "invalid");
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

	bool noErrors = false;
	do {
		if (auto startToken = file_.getToken(BasicType::Name)) {
			if (tokenValue(startToken) == "using") {
				if (auto includedResult = readIncluded()) {
					result_.includes.push_back(includedResult);
					continue;
				}
			} else if (auto braceOpen = file_.getToken(BasicType::LeftBrace)) {
				if (auto structResult = readStruct(tokenValue(startToken))) {
					result_.structs.push_back(structResult);
					continue;
				}
			} else if (auto colonToken = file_.getToken(BasicType::Colon)) {
				if (auto variableResult = readVariable(tokenValue(startToken))) {
					result_.variables.push_back(variableResult);
					continue;
				}
			}
		}
		if (!file_.atEnd()) {
			logErrorUnexpectedToken() << "using keyword, or struct definition, or variable definition";
		} else {
			noErrors = !failed();
			break;
		}
	} while (!failed());

	if (noErrors) {
		result_.fullpath = QFileInfo(options_.inputPath).absoluteFilePath();
	}
	return noErrors;
}

common::LogStream ParsedFile::logErrorTypeMismatch() {
	return logError(kErrorTypeMismatch) << "type mismatch: ";
}

structure::Module ParsedFile::readIncluded() {
	structure::Module result;
	if (auto usingFile = file_.getToken(BasicType::String)) {
		if (file_.getToken(BasicType::Semicolon)) {
			ParsedFile included(includedOptions(tokenValue(usingFile)));
			if (included.read()) {
				result = included.data();
			} else {
				logError(kErrorInIncluded) << "error while parsing '" << tokenValue(usingFile).toStdString() << "'";
			}
		} else {
			logErrorUnexpectedToken() << "';'";
		}
	} else {
		logErrorUnexpectedToken() << "file path";
	}
	return result;
}

structure::Struct ParsedFile::readStruct(const QString &name) {
	structure::Struct result = { composeFullName(name) };
	do {
		if (auto fieldName = file_.getToken(BasicType::Name)) {
			if (auto field = readStructField(tokenValue(fieldName))) {
				result.fields.push_back(field);
			}
		} else if (auto braceClose = file_.getToken(BasicType::RightBrace)) {
			if (result.fields.isEmpty()) {
				logErrorUnexpectedToken() << "at least one field in struct";
			}
			break;
		} else {
			logErrorUnexpectedToken() << "struct field name or '}'";
		}
	} while (!failed());
	return result;
}

structure::Variable ParsedFile::readVariable(const QString &name) {
	structure::Variable result = { composeFullName(name) };
	if (auto value = readValue()) {
		result.value = value;
		if (!file_.getToken(BasicType::Semicolon)) {
			logErrorUnexpectedToken() << "';'";
		}
	}
	return result;
}

structure::StructField ParsedFile::readStructField(const QString &name) {
	structure::StructField result = { composeFullName(name) };
	if (auto colonToken = file_.getToken(BasicType::Colon)) {
		if (auto type = readType()) {
			result.type = type;
			if (!file_.getToken(BasicType::Semicolon)) {
				logErrorUnexpectedToken() << "';'";
			}
		}
	} else {
		logErrorUnexpectedToken() << "':'";
	}
	return result;
}

structure::Type ParsedFile::readType() {
	structure::Type result;
	if (auto nameToken = file_.getToken(BasicType::Name)) {
		auto name = tokenValue(nameToken);
		if (auto builtInType = typeNames_.value(name.toStdString())) {
			result = builtInType;
		} else {
			result.tag = structure::TypeTag::Struct;
			result.name = composeFullName(name);
		}
	} else {
		logErrorUnexpectedToken() << "type name";
	}
	return result;
}

structure::Value ParsedFile::readValue() {
	if (auto colorValue = readColorValue()) {
		return colorValue;
	} else if (auto pointValue = readPointValue()) {
		return pointValue;
	} else if (auto spriteValue = readSpriteValue()) {
		return spriteValue;
	} else if (auto sizeValue = readSizeValue()) {
		return sizeValue;
	} else if (auto transitionValue = readTransitionValue()) {
		return transitionValue;
	} else if (auto cursorValue = readCursorValue()) {
		return cursorValue;
	} else if (auto alignValue = readAlignValue()) {
		return alignValue;
	} else if (auto marginsValue = readMarginsValue()) {
		return marginsValue;
	} else if (auto fontValue = readFontValue()) {
		return fontValue;
	} else if (auto numericValue = readNumericValue()) {
		return numericValue;
	} else if (auto stringValue = readStringValue()) {
		return stringValue;
	} else if (auto structValue = readStructValue()) {
		return structValue;
	} else if (auto copyValue = readCopyValue()) {
		return copyValue;
	} else {
		logErrorUnexpectedToken() << "variable value";
	}
	return {};
}

structure::Value ParsedFile::readStructValue() {
	if (auto structName = file_.getToken(BasicType::Name)) {
		if (auto result = defaultConstructedStruct(composeFullName(tokenValue(structName)))) {
			if (file_.getToken(BasicType::LeftParenthesis)) {
				if (readStructParents(result)) {
					if (file_.getToken(BasicType::LeftBrace)) {
						readStructValueInner(result);
					} else {
						logErrorUnexpectedToken() << "'{'";
					}
				}
			} else if (file_.getToken(BasicType::LeftBrace)) {
				readStructValueInner(result);
			} else {
				logErrorUnexpectedToken() << "'(' or '{'";
			}
			return result;
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::defaultConstructedStruct(const structure::FullName &structName) {
	structure::Value result;
	for (const auto &structType : result_.structs) {
		if (structType.name == structName) {
			result.fields.reserve(structType.fields.size());
			for (const auto &fieldType : structType.fields) {
				result.fields.push_back({ fieldType.name, fieldType.type });
			}
		}
	}
	return result;
}

void ParsedFile::applyStructParent(structure::Value &result, const structure::FullName &parentName) {
	for (const auto &structValue : result_.variables) {
		if (structValue.name == parentName) {
			if (structValue.value.type == result.type) {
				const auto &srcFields(structValue.value.fields);
				auto &dstFields(result.fields);
				logAssert(srcFields.size() == dstFields.size()) << "struct size check failed";

				for (int i = 0, s = srcFields.size(); i != s; ++i) {
					logAssert(srcFields.at(i).value.type == dstFields.at(i).value.type) << "struct field type check failed";
					dstFields[i].value = srcFields.at(i).value;
				}
			} else {
				logErrorTypeMismatch() << "parent '" << logFullName(parentName) << "' has type '" << logType(structValue.value.type) << "' while child value has type " << logType(result.type);
			}
		}
	}
}

bool ParsedFile::readStructValueInner(structure::Value &result) {
	do {
		if (auto fieldName = file_.getToken(BasicType::Name)) {
			if (auto field = readVariable(tokenValue(fieldName))) {
				for (auto &already : result.fields) {
					if (already.name == field.name) {
						if (already.value.type == field.value.type) {
							already.value = field.value;
							return true;
						} else {
							logErrorTypeMismatch() << "field '" << logFullName(already.name) << "' has type '" << logType(already.value.type) << "' while value has type " << logType(field.value.type);
							return false;
						}
					}
				}
				logError(kErrorUnknownField) << "field '" << logFullName(field.name) << "' was not found in struct of type '" << logType(result.type) << "'";
			}
		} else if (file_.getToken(BasicType::RightBrace)) {
			return true;
		} else {
			logErrorUnexpectedToken() << "variable field name or '}'";
		}
	} while (!failed());
	return false;
}

bool ParsedFile::readStructParents(structure::Value &result) {
	do {
		if (auto parentName = file_.getToken(BasicType::Name)) {
			applyStructParent(result, composeFullName(tokenValue(parentName)));
			if (file_.getToken(BasicType::RightParenthesis)) {
				return true;
			} else if (!file_.getToken(BasicType::Comma)) {
				logErrorUnexpectedToken() << "',' or ')'";
			}
		} else {
			logErrorUnexpectedToken() << "struct variable parent";
		}
	} while (!failed());
	return false;
}

//ParsedFile::Token ParsedFile::readInVariableChild() {
//	if (auto value = readValue()) {
//		if (file_.getToken(BasicType::Semicolon)) {
//			state_ = State::Default;
//			return value;
//		}
//		logErrorUnexpectedToken(";");
//	} else {
//		logErrorUnexpectedToken("variable field value");
//	}
//	return invalidToken();
//}
//
structure::Value ParsedFile::readPositiveValue() {
	auto numericToken = file_.getAnyToken();
	if (numericToken.type == BasicType::Int) {
		return { { structure::TypeTag::Int }, tokenValue(numericToken) };
	} else if (numericToken.type == BasicType::Double) {
		return { { structure::TypeTag::Double }, tokenValue(numericToken) };
	} else if (numericToken.type == BasicType::Name) {
		auto value = tokenValue(numericToken);
		auto match = QRegularExpression("^\\d+px$").match(value);
		if (match.hasMatch()) {
			return { { structure::TypeTag::Pixels }, value.mid(0, value.size() - 2) };
		}
	}
	file_.putBack();
	return {};
}

structure::Value ParsedFile::readNumericValue() {
	if (auto value = readPositiveValue()) {
		return value;
	} else if (auto minusToken = file_.getToken(BasicType::Minus)) {
		if (auto positiveValue = readNumericValue()) {
			return { positiveValue.type, '-' + positiveValue.data };
		}
		logErrorUnexpectedToken() << "numeric value";
	}
	return {};
}

structure::Value ParsedFile::readStringValue() {
	if (auto stringToken = file_.getToken(BasicType::String)) {
		return { { structure::TypeTag::String }, stringToken.value };
	}
	return {};
}

structure::Value ParsedFile::readColorValue() {
	if (auto numberSign = file_.getToken(BasicType::Number)) {
		auto color = file_.getAnyToken();
		if (color.type == BasicType::Int || color.type == BasicType::Name) {
			auto chars = tokenValue(color);
			if (isValidColor(chars)) {
				return { { structure::TypeTag::Color }, chars.toLower() };
			}
		} else {
			logErrorUnexpectedToken() << "color value in #ccc, #ccca, #cccccc or #ccccccaa format";
		}
	}
	return {};
}

structure::Value ParsedFile::readPointValue() {
	return {};
}

structure::Value ParsedFile::readSpriteValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "sprite") {
			if (!file_.getToken(BasicType::LeftParenthesis)) {
				logErrorUnexpectedToken() << "'(' and sprite definition";
				return {};
			}

			auto x = readNumericValue(); file_.getToken(BasicType::Comma);
			auto y = readNumericValue(); file_.getToken(BasicType::Comma);
			auto w = readNumericValue(); file_.getToken(BasicType::Comma);
			auto h = readNumericValue();
			if (x.type.tag != structure::TypeTag::Pixels ||
				y.type.tag != structure::TypeTag::Pixels ||
				w.type.tag != structure::TypeTag::Pixels ||
				h.type.tag != structure::TypeTag::Pixels) {
				logErrorTypeMismatch() << "px rect for the sprite expected";
				return {};
			}

			if (!file_.getToken(BasicType::RightParenthesis)) {
				logErrorUnexpectedToken() << "')'";
				return {};
			}

			return { { structure::TypeTag::Sprite }, x.data + ',' + y.data + ',' + w.data + ',' + h.data };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readSizeValue() {
	return {};
}

structure::Value ParsedFile::readTransitionValue() {
	return {};
}

structure::Value ParsedFile::readCursorValue() {
	return {};
}

structure::Value ParsedFile::readAlignValue() {
	return {};
}

structure::Value ParsedFile::readMarginsValue() {
	return {};
}

structure::Value ParsedFile::readFontValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "font") {
			if (!file_.getToken(BasicType::LeftParenthesis)) {
				logErrorUnexpectedToken() << "'(' and font definition";
				return {};
			}

			structure::Value family, size;
			do {
				if (auto familyValue = readStringValue()) {
					family = familyValue;
				} else if (auto sizeValue = readNumericValue()) {
					size = sizeValue;
				} else if (auto copyValue = readCopyValue()) {
					if (copyValue.type.tag == structure::TypeTag::String) {
						family = copyValue;
					} else if (copyValue.type.tag == structure::TypeTag::Pixels) {
						size = copyValue;
					} else {
						logErrorUnexpectedToken() << "font family, font size or ')'";
					}
				} else if (file_.getToken(BasicType::RightParenthesis)) {
					break;
				} else {
					logErrorUnexpectedToken() << "font family, font size or ')'";
				}
			} while (!failed());

			if (size.type.tag != structure::TypeTag::Pixels) {
				logErrorTypeMismatch() << "px value for the font size expected";
			}
			return { { structure::TypeTag::Font }, size.data + ',' + family.data };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readCopyValue() {
	if (auto copyName = file_.getToken(BasicType::Name)) {
		structure::FullName name = { tokenValue(copyName) };
		for (const auto &variable : result_.variables) {
			if (variable.name == name) {
				auto result = variable.value;
				result.copy = variable.name;
				return result;
			}
		}
		logError(kErrorIdentifierNotFound) << "identifier '" << logFullName(name) << "' not found";
	}
	return {};
}

Options ParsedFile::includedOptions(const QString &filepath) {
	auto result = options_;
	result.inputPath = filepath;
	return result;
}

// Compose context-dependent full name.
structure::FullName ParsedFile::composeFullName(const QString &name) {
	return { name };
}

} // namespace style
} // namespace codegen
