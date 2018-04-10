/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	static const auto validator = QRegularExpression("^[a-z0-9_.-]+(#(one|other))?$", QRegularExpression::CaseInsensitiveOption);
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

const std::array<QString, kPluralPartCount> kPluralParts = { {
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other",
} };

const QString kPluralTag = "count";

QString ComputePluralKey(const QString &base, int index) {
	return base + "__plural" + QString::number(index);
}

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
				logErrorUnexpectedToken() << "string key name (/^[a-z0-9_.-]+(#(one|other))?$/i)";
			}
		}
		if (file_.atEnd()) {
			break;
		}
		logErrorUnexpectedToken() << "ansi string key name";
	} while (!failed());

	fillPluralTags();

	return !failed();
}

void ParsedFile::fillPluralTags() {
	auto count = result_.entries.size();
	for (auto i = 0; i != count;) {
		auto &baseEntry = result_.entries[i];
		if (baseEntry.keyBase.isEmpty()) {
			++i;
			continue;
		}
		logAssert(i + kPluralPartCount < count);

		// Accumulate all tags from all plural variants.
		auto tags = std::vector<LangPack::Tag>();
		for (auto j = i; j != i + kPluralPartCount; ++j) {
			if (tags.empty()) {
				tags = result_.entries[j].tags;
			} else {
				for (auto &tag : result_.entries[j].tags) {
					if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
						tags.push_back(tag);
					}
				}
			}
		}
		logAssert(!tags.empty());
		logAssert(tags.front().tag == kPluralTag);

		// Set this tags list to all plural variants.
		for (auto j = i; j != i + kPluralPartCount; ++j) {
			result_.entries[j].tags = tags;
		}

		i += kPluralPartCount;
	}
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

void ParsedFile::addEntity(QString key, const QString &value) {
	auto pluralPartOffset = key.indexOf('#');
	auto pluralIndex = -1;
	if (pluralPartOffset >= 0) {
		auto pluralPart = key.mid(pluralPartOffset + 1);
		pluralIndex = std::find(kPluralParts.begin(), kPluralParts.end(), pluralPart) - kPluralParts.begin();
		if (pluralIndex < 0 || pluralIndex >= kPluralParts.size()) {
			logError(kErrorBadString) << "bad plural part for key '" << key.toStdString() << "': '" << pluralPart.toStdString() << "'";
			return;
		}
		key = key.mid(0, pluralPartOffset);
	}
	auto checkKey = [this](const QString &key) {
		for (auto &entry : result_.entries) {
			if (entry.key == key) {
				if (entry.keyBase.isEmpty() || !entry.tags.empty()) {
					// Empty tags in plural entry means it was not encountered yet.
					logError(kErrorBadString) << "duplicate found for key '" << key.toStdString() << "'";
					return false;
				}
			}
		}
		return true;
	};
	if (!checkKey(key)) {
		return;
	}
	auto tagsData = LangPack();
	auto entry = LangPack::Entry();
	entry.key = key;
	entry.value = extractTagsData(value, &tagsData);
	entry.tags = tagsData.tags;
	if (pluralIndex >= 0) {
		logAssert(tagsData.entries.empty());

		entry.keyBase = entry.key;
		entry.key = ComputePluralKey(entry.keyBase, pluralIndex);
		if (!checkKey(entry.key)) {
			return;
		}
		auto baseIndex = -1;
		auto alreadyCount = result_.entries.size();
		for (auto i = 0; i != alreadyCount; ++i) {
			if (result_.entries[i].keyBase == entry.keyBase) {
				// This is not the first appearance of this plural key.
				baseIndex = i;
				break;
			}
		}
		if (baseIndex < 0) {
			baseIndex = result_.entries.size();
			for (auto i = 0; i != kPluralPartCount; ++i) {
				auto addingEntry = LangPack::Entry();
				addingEntry.keyBase = entry.keyBase;
				addingEntry.key = ComputePluralKey(entry.keyBase, i);
				result_.entries.push_back(addingEntry);
			}
		}
		auto entryIndex = baseIndex + pluralIndex;
		logAssert(entryIndex < result_.entries.size());
		auto &realEntry = result_.entries[entryIndex];
		logAssert(realEntry.key == entry.key);
		realEntry.value = entry.value;

		// Add all new tags to the existing ones.
		realEntry.tags = std::vector<LangPack::Tag>(1, LangPack::Tag { kPluralTag });
		for (auto &tag : entry.tags) {
			if (std::find(realEntry.tags.begin(), realEntry.tags.end(), tag) == realEntry.tags.end()) {
				realEntry.tags.push_back(tag);
			}
		}
	} else {
		result_.entries.push_back(entry);
		for (auto &pluralEntry : tagsData.entries) {
			auto taggedEntry = LangPack::Entry();
			taggedEntry.key = key + "__" + pluralEntry.key;
			taggedEntry.value = pluralEntry.value;
			result_.entries.push_back(taggedEntry);
		}
	}
}

} // namespace lang
} // namespace codegen
