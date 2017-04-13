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
#include "codegen/lang/parsed_file.h"

#include <iostream>
#include <QtCore/QMap>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/common/logging.h"

namespace codegen {
namespace lang {
namespace {

using BasicToken = codegen::common::BasicTokenizedFile::Token;
using BasicType = BasicToken::Type;

constexpr int kErrorBadString          = 806;

bool ValidateAnsiString(const QString &value) {
	for (auto ch : value) {
		if (ch.unicode() > 127) {
			return false;
		}
	}
	return true;
}

bool ValidateKey(const QString &key) {
	static const auto validator = QRegularExpression("^[a-z0-9_.-]+$", QRegularExpression::CaseInsensitiveOption);
	if (!validator.match(key).hasMatch()) {
		return false;
	}
	if (key.indexOf("__") >= 0) {
		return false;
	}
	return true;
}

bool ValidateTag(const QString &tag) {
	static const auto validator = QRegularExpression("^[a-z0-9_]+$", QRegularExpression::CaseInsensitiveOption);
	if (!validator.match(tag).hasMatch()) {
		return false;
	}
	if (tag.indexOf("__") >= 0) {
		return false;
	}
	return true;
}

QString PrepareCommandString(int index) {
	static const QChar TextCommand(0x0010);
	static const QChar TextCommandLangTag(0x0020);
	auto result = QString(4, TextCommand);
	result[1] = TextCommandLangTag;
	result[2] = QChar(0x0020 + ushort(index));
	return result;
}

} // namespace

ParsedFile::ParsedFile(const Options &options)
: filePath_(options.inputPath)
, file_(filePath_)
, options_(options) {
}

bool ParsedFile::read() {
	if (!file_.read()) {
		return false;
	}

	do {
		if (auto keyToken = file_.getToken(BasicType::String)) {
			if (ValidateKey(keyToken.value)) {
				if (auto equals = file_.getToken(BasicType::Equals)) {
					if (auto valueToken = file_.getToken(BasicType::String)) {
						assertNextToken(BasicType::Semicolon);
						addEntity(keyToken.value, valueToken.value);
						continue;
					} else {
						logErrorUnexpectedToken() << "string value for '" << keyToken.value.toStdString() << "' key";
					}
				} else {
					logErrorUnexpectedToken() << "'=' for '" << keyToken.value.toStdString() << "' key";
				}
			} else {
				logErrorUnexpectedToken() << "string key name (/^[a-z0-9_.-]+$/i)";
			}
		}
		if (file_.atEnd()) {
			break;
		}
		logErrorUnexpectedToken() << "ansi string key name";
	} while (!failed());

	return !failed();
}

BasicToken ParsedFile::assertNextToken(BasicToken::Type type) {
	auto result = file_.getToken(type);
	if (!result) {
		logErrorUnexpectedToken() << type;
	}
	return result;
}

common::LogStream ParsedFile::logErrorBadString() {
	return logError(kErrorBadString);
}

QString ParsedFile::extractTagsData(const QString &value, LangPack *to) {
	auto tagStart = value.indexOf('{');
	if (tagStart < 0) {
		return value;
	}

	auto tagEnd = 0;
	auto finalValue = QString();
	finalValue.reserve(value.size() * 2);
	while (tagStart >= 0) {
		if (tagStart > tagEnd) {
			finalValue.append(value.midRef(tagEnd, tagStart - tagEnd));
		}
		++tagStart;
		tagEnd = value.indexOf('}', tagStart);
		if (tagEnd < 0) {
			logErrorBadString() << "unexpected end of value, end of tag expected.";
			return value;
		}
		finalValue.append(extractTagData(value.mid(tagStart, tagEnd - tagStart), to));
		++tagEnd;
		tagStart = value.indexOf('{', tagEnd);
	}
	if (tagEnd < value.size()) {
		finalValue.append(value.midRef(tagEnd));
	}
	return finalValue;
}

QString ParsedFile::extractTagData(const QString &tagText, LangPack *to) {
	auto numericPart = tagText.indexOf(':');
	auto tag = (numericPart > 0) ? tagText.mid(0, numericPart) : tagText;
	if (!ValidateTag(tag)) {
		logErrorBadString() << "bad tag characters: '" << tagText.toStdString() << "'";
		return QString();
	}
	for (auto &previousTag : to->tags) {
		if (previousTag.tag == tag) {
			logErrorBadString() << "duplicate found for tag '" << tagText.toStdString() << "'";
			return QString();
		}
	}
	auto index = 0;
	auto tagIndex = result_.tags.size();
	for (auto &alreadyTag : result_.tags) {
		if (alreadyTag.tag == tag) {
			tagIndex = index;
			break;
		}
		++index;
	}
	if (tagIndex == result_.tags.size()) {
		result_.tags.push_back({ tag });
	}
	if (numericPart > 0) {
		auto numericParts = tagText.mid(numericPart + 1).split('|');
		if (numericParts.size() != 3) {
			logErrorBadString() << "bad option count for plural key part in tag: '" << tagText.toStdString() << "'";
			return QString();
		}
		auto index = 0;
		for (auto &part : numericParts) {
			auto numericPartEntry = LangPack::Entry();
			numericPartEntry.key = tag + QString::number(index++);
			if (part.indexOf('#') != part.lastIndexOf('#')) {
				logErrorBadString() << "bad option for plural key part in tag: '" << tagText.toStdString() << "', too many '#'.";
				return QString();
			}
			numericPartEntry.value = part.replace('#', PrepareCommandString(tagIndex));
			to->entries.push_back(numericPartEntry);
		}
	}
	to->tags.push_back({ tag });
	return PrepareCommandString(tagIndex);
}

void ParsedFile::addEntity(const QString &key, const QString &value) {
	for (auto &entry : result_.entries) {
		if (entry.key == key) {
			logError(kErrorBadString) << "duplicate found for key '" << key.toStdString() << "'";
			return;
		}
	}
	auto tagsData = LangPack();
	auto entry = LangPack::Entry();
	entry.key = key;
	entry.value = extractTagsData(value, &tagsData);
	entry.tags = tagsData.tags;
	result_.entries.push_back(entry);
	for (auto &pluralEntry : tagsData.entries) {
		auto taggedEntry = LangPack::Entry();
		taggedEntry.key = key + "__" + pluralEntry.key;
		taggedEntry.value = pluralEntry.value;
		result_.entries.push_back(taggedEntry);
	}
}

} // namespace lang
} // namespace codegen
