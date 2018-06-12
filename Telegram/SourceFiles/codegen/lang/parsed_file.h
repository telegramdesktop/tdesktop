/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <string>
#include <array>
#include <functional>
#include <QImage>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/lang/options.h"

namespace codegen {
namespace lang {

constexpr auto kPluralPartCount = 6;
extern const std::array<QString, kPluralPartCount> kPluralParts;
extern const QString kPluralTag;
QString ComputePluralKey(const QString &base, int index);

struct LangPack {
	struct Tag {
		QString tag;
	};
	struct Entry {
		QString key;
		QString value;
		QString keyBase; // Empty for not plural entries.
		std::vector<Tag> tags;
	};
	std::vector<Entry> entries;
	std::vector<Tag> tags;

};

inline bool operator==(const LangPack::Tag &a, const LangPack::Tag &b) {
	return a.tag == b.tag;
}

inline bool operator!=(const LangPack::Tag &a, const LangPack::Tag &b) {
	return !(a == b);
}

// Parses an input file to the internal struct.
class ParsedFile {
public:
	explicit ParsedFile(const Options &options);
	ParsedFile(const ParsedFile &other) = delete;
	ParsedFile &operator=(const ParsedFile &other) = delete;

	bool read();

	LangPack getResult() {
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
	common::LogStream logErrorBadString();
	common::LogStream logAssert(bool assertion) {
		if (!assertion) {
			return logError(common::kErrorInternal) << "internal - ";
		}
		return common::LogStream(common::LogStream::Null);
	}

	// Read next token and fire unexpected token error if it is not of "type".
	using BasicToken = common::BasicTokenizedFile::Token;
	BasicToken assertNextToken(BasicToken::Type type);

	void addEntity(QString key, const QString &value);
	QString extractTagsData(const QString &value, LangPack *to);
	QString extractTagData(const QString &tag, LangPack *to);

	void fillPluralTags();

	QString filePath_;
	common::BasicTokenizedFile file_;
	Options options_;
	bool failed_ = false;
	LangPack result_;

};

} // namespace lang
} // namespace codegen
