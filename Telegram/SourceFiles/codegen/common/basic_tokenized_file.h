/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QList>

#include "codegen/common/const_utf8_string.h"
#include "codegen/common/clean_file_reader.h"

namespace codegen {
namespace common {

class LogStream;

// Interface for reading a cleaned from comments file by basic tokens.
class BasicTokenizedFile {
public:
	explicit BasicTokenizedFile(const QString &filepath);
	explicit BasicTokenizedFile(const QByteArray &content, const QString &filepath = QString());
	BasicTokenizedFile(const BasicTokenizedFile &other) = delete;
	BasicTokenizedFile &operator=(const BasicTokenizedFile &other) = delete;

	struct Token {
		// String - utf8 string converted to QString.
		enum class Type {
			Invalid = 0,
			Int,
			Double,
			String,
			LeftParenthesis,
			RightParenthesis,
			LeftBrace,
			RightBrace,
			LeftBracket,
			RightBracket,
			Colon,
			Semicolon,
			Comma,
			Dot,
			Number,
			Plus,
			Minus,
			Equals,
			And,
			Or,
			Name, // [0-9a-zA-Z_]+ with at least one letter.
		};
		Type type;
		QString value;
		ConstUtf8String original;
		bool hasLeftWhitespace;

		explicit operator bool() const {
			return (type != Type::Invalid);
		}
	};

	bool read() {
		if (reader_.read()) {
			singleLineComments_ = reader_.singleLineComments();
			return true;
		}
		return false;
	}
	bool atEnd() const {
		return reader_.atEnd();
	}

	Token getAnyToken();
	Token getToken(Token::Type typeCondition);
	bool putBack();
	bool failed() const {
		return failed_;
	}

	QString getCurrentLineComment();

	// Log error to std::cerr with 'code' at the current position in file.
	LogStream logError(int code) const;
	LogStream logErrorUnexpectedToken() const;

	~BasicTokenizedFile();

private:
	using Type = Token::Type;

	void skipWhitespaces();

	// Reads a token, including complex tokens, like double numbers.
	Type readToken();

	// Read exactly one token, applying condition on the whitespaces.
	enum class StartWithWhitespace {
		Allow,
		Deny,
	};
	Type readOneToken(StartWithWhitespace condition);

	// helpers
	Type readNameOrNumber();
	Type readString();
	Type readSingleLetter();

	Type saveToken(Type type, const QString &value = QString());
	Type uniteLastTokens(Type type);

	CleanFileReader reader_;
	QList<Token> tokens_;
	int currentToken_ = 0;
	int lineNumber_ = 1;
	bool failed_ = false;
	QVector<QByteArray> singleLineComments_;

	// Where the last (currently read) token has started.
	const char *tokenStart_ = nullptr;

	// Did the last (currently read) token start with a whitespace.
	bool tokenStartWhitespace_ = false;

	const QMap<char, Type> singleLetterTokens_ = {
		{ '(', Type::LeftParenthesis },
		{ ')', Type::RightParenthesis },
		{ '{', Type::LeftBrace },
		{ '}', Type::RightBrace },
		{ '[', Type::LeftBracket },
		{ ']', Type::RightBracket },
		{ ':', Type::Colon },
		{ ';', Type::Semicolon },
		{ ',', Type::Comma },
		{ '.', Type::Dot },
		{ '#', Type::Number },
		{ '+', Type::Plus },
		{ '-', Type::Minus },
		{ '=', Type::Equals },
		{ '&', Type::And },
		{ '|', Type::Or },
	};

};

LogStream operator<<(LogStream &&stream, BasicTokenizedFile::Token::Type type);
template <>
LogStream operator<< <BasicTokenizedFile::Token::Type>(LogStream &&stream, BasicTokenizedFile::Token::Type &&value) = delete;
template <>
LogStream operator<< <const BasicTokenizedFile::Token::Type&>(LogStream &&stream, const BasicTokenizedFile::Token::Type &value) = delete;

} // namespace common
} // namespace codegen
