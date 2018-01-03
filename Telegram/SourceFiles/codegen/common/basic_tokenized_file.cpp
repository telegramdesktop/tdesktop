/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/common/basic_tokenized_file.h"

#include "codegen/common/logging.h"
#include "codegen/common/clean_file_reader.h"
#include "codegen/common/checked_utf8_string.h"

using Token = codegen::common::BasicTokenizedFile::Token;
using Type = Token::Type;

namespace codegen {
namespace common {
namespace {

constexpr int kErrorUnterminatedStringLiteral = 201;
constexpr int kErrorIncorrectUtf8String       = 202;
constexpr int kErrorIncorrectToken            = 203;
constexpr int kErrorUnexpectedToken           = 204;

bool isDigitChar(char ch) {
	return (ch >= '0') && (ch <= '9');
}

bool isNameChar(char ch) {
	return isDigitChar(ch) || ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) || (ch == '_');
}

bool isWhitespaceChar(char ch) {
	return (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t');
}

Token invalidToken() {
	return { Type::Invalid, QString(), ConstUtf8String(nullptr, 0), false };
}

} // namespace

BasicTokenizedFile::BasicTokenizedFile(const QString &filepath) : reader_(filepath) {
}

BasicTokenizedFile::BasicTokenizedFile(const QByteArray &content, const QString &filepath) : reader_(content, filepath) {
}

bool BasicTokenizedFile::putBack() {
	if (currentToken_ > 0) {
		--currentToken_;
		return true;
	}
	return false;
}

Token BasicTokenizedFile::getAnyToken() {
	if (currentToken_ >= tokens_.size()) {
		if (readToken() == Type::Invalid) {
			return invalidToken();
		}
	}
	return tokens_.at(currentToken_++);
}

Token BasicTokenizedFile::getToken(Type typeCondition) {
	if (auto token = getAnyToken()) {
		if (token.type == typeCondition) {
			return token;
		}
		putBack();
	}
	return invalidToken();
}

Type BasicTokenizedFile::readToken() {
	auto result = readOneToken(StartWithWhitespace::Allow);

	// Try to read double token.
	if (result == Type::Int) {
		if (readOneToken(StartWithWhitespace::Deny) == Type::Dot) {
			// We got int and dot, so it is double already.
			result = uniteLastTokens(Type::Double);

			// Try to read one more int (after dot).
			if (readOneToken(StartWithWhitespace::Deny) == Type::Int) {
				result = uniteLastTokens(Type::Double);
			}
		}
	} else if (result == Type::Dot) {
		if (readOneToken(StartWithWhitespace::Deny) == Type::Int) {
			//We got dot and int, so it is double.
			result = uniteLastTokens(Type::Double);
		}
	}
	return result;
}

Type BasicTokenizedFile::readOneToken(StartWithWhitespace condition) {
	skipWhitespaces();
	if (tokenStartWhitespace_ && condition == StartWithWhitespace::Deny) {
		return Type::Invalid;
	}
	if (reader_.atEnd()) {
		return Type::Invalid;
	}

	auto ch = reader_.currentChar();
	if (ch == '"') {
		return readString();
	} else if (isNameChar(ch)) {
		return readNameOrNumber();
	}
	return readSingleLetter();
}

Type BasicTokenizedFile::saveToken(Type type, const QString &value) {
	ConstUtf8String original = { tokenStart_, reader_.currentPtr() };
	tokens_.push_back({ type, value, original, tokenStartWhitespace_ });
	return type;
}

Type BasicTokenizedFile::uniteLastTokens(Type type) {
	auto size = tokens_.size();
	if (size < 2) {
		return Type::Invalid;
	}

	auto &token(tokens_[size - 2]);
	auto originalFrom = token.original.data();
	auto originalTill = tokens_.back().original.end();
	token.type = type;
	token.original = { originalFrom, originalTill };
	token.value += tokens_.back().value;
	tokens_.pop_back();
	return type;
}

QString BasicTokenizedFile::getCurrentLineComment() {
	if (lineNumber_ > singleLineComments_.size()) {
		reader_.logError(kErrorInternal, lineNumber_) << "internal tokenizer error (line number larger than comments list size).";
		failed_ = true;
		return QString();
	}
	auto commentBytes = singleLineComments_[lineNumber_ - 1].mid(2); // Skip "//"
	CheckedUtf8String comment(commentBytes);
	if (!comment.isValid()) {
		reader_.logError(kErrorIncorrectUtf8String, lineNumber_) << "incorrect UTF-8 string in the comment.";
		failed_ = true;
		return QString();
	}
	return comment.toString().trimmed();
}

Type BasicTokenizedFile::readNameOrNumber() {
	while (!reader_.atEnd()) {
		if (!isDigitChar(reader_.currentChar())) {
			break;
		}
		reader_.skipChar();
	}
	bool onlyDigits = true;
	while (!reader_.atEnd()) {
		if (!isNameChar(reader_.currentChar())) {
			break;
		}
		onlyDigits = false;
		reader_.skipChar();
	}
	return saveToken(onlyDigits ? Type::Int : Type::Name);
}

Type BasicTokenizedFile::readString() {
	reader_.skipChar();
	auto offset = reader_.currentPtr();

	QByteArray value;
	while (!reader_.atEnd()) {
		auto ch = reader_.currentChar();
		if (ch == '"') {
			if (reader_.currentPtr() > offset) {
				value.append(offset, reader_.currentPtr() - offset);
			}
			break;
		}
		if (ch == '\n') {
			reader_.logError(kErrorUnterminatedStringLiteral, lineNumber_) << "unterminated string literal.";
			failed_ = true;
			return Type::Invalid;
		}
		if (ch == '\\') {
			if (reader_.currentPtr() > offset) {
				value.append(offset, reader_.currentPtr() - offset);
			}
			reader_.skipChar();
			ch = reader_.currentChar();
			if (reader_.atEnd() || ch == '\n') {
				reader_.logError(kErrorUnterminatedStringLiteral, lineNumber_) << "unterminated string literal.";
				failed_ = true;
				return Type::Invalid;
			}
			offset = reader_.currentPtr() + 1;
			if (ch == 'n') {
				value.append('\n');
			} else if (ch == 't') {
				value.append('\t');
			} else if (ch == '"') {
				value.append('"');
			} else if (ch == '\\') {
				value.append('\\');
			}
		}
		reader_.skipChar();
	}
	if (reader_.atEnd()) {
		reader_.logError(kErrorUnterminatedStringLiteral, lineNumber_) << "unterminated string literal.";
		failed_ = true;
		return Type::Invalid;
	}
	CheckedUtf8String checked(value);
	if (!checked.isValid()) {
		reader_.logError(kErrorIncorrectUtf8String, lineNumber_) << "incorrect UTF-8 string literal.";
		failed_ = true;
		return Type::Invalid;
	}
	reader_.skipChar();
	return saveToken(Type::String, checked.toString());
}

Type BasicTokenizedFile::readSingleLetter() {
	auto type = singleLetterTokens_.value(reader_.currentChar(), Type::Invalid);
	if (type == Type::Invalid) {
		reader_.logError(kErrorIncorrectToken, lineNumber_) << "incorrect token '" << reader_.currentChar() << "'";
		return Type::Invalid;
	}

	reader_.skipChar();
	return saveToken(type);
}

void BasicTokenizedFile::skipWhitespaces() {
	if (reader_.atEnd()) return;

	auto ch = reader_.currentChar();
	tokenStartWhitespace_ = isWhitespaceChar(ch);
	if (tokenStartWhitespace_) {
		do {
			if (ch == '\n') {
				++lineNumber_;
			}
			reader_.skipChar();
			ch = reader_.currentChar();
		} while (!reader_.atEnd() && isWhitespaceChar(ch));
	}
	tokenStart_ = reader_.currentPtr();
}

LogStream operator<<(LogStream &&stream, BasicTokenizedFile::Token::Type type) {
	const char *value = "'invalid'";
	switch (type) {
	case Type::Invalid: break;
	case Type::Int: value = "'int'"; break;
	case Type::Double: value = "'double'"; break;
	case Type::String: value = "'string'"; break;
	case Type::LeftParenthesis: value = "'('"; break;
	case Type::RightParenthesis: value = "')'"; break;
	case Type::LeftBrace: value = "'{'"; break;
	case Type::RightBrace: value = "'}'"; break;
	case Type::LeftBracket: value = "'['"; break;
	case Type::RightBracket: value = "']'"; break;
	case Type::Colon: value = "':'"; break;
	case Type::Semicolon: value = "';'"; break;
	case Type::Comma: value = "','"; break;
	case Type::Dot: value = "'.'"; break;
	case Type::Number: value = "'#'"; break;
	case Type::Plus: value = "'+'"; break;
	case Type::Minus: value = "'-'"; break;
	case Type::Equals: value = "'='"; break;
	case Type::Name: value = "'identifier'"; break;
	}
	return std::forward<LogStream>(stream) << value;
}

LogStream BasicTokenizedFile::logError(int code) const {
	return reader_.logError(code, lineNumber_);
}

LogStream BasicTokenizedFile::logErrorUnexpectedToken() const {
	if (currentToken_ < tokens_.size()) {
		auto token = tokens_.at(currentToken_).original.toStdString();
		return logError(kErrorUnexpectedToken) << "unexpected token '" << token << "', expected ";
	}
	return logError(kErrorUnexpectedToken) << "unexpected token, expected ";
}

BasicTokenizedFile::~BasicTokenizedFile() = default;

} // namespace common
} // namespace codegen
