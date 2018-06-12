/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/lang/generator.h"

#include <memory>
#include <functional>
#include <QtCore/QDir>
#include <QtCore/QSet>
#include <QtGui/QImage>
#include <QtGui/QPainter>

namespace codegen {
namespace lang {
namespace {

char hexChar(uchar ch) {
	if (ch < 10) {
		return '0' + ch;
	} else if (ch < 16) {
		return 'a' + (ch - 10);
	}
	return '0';
}

char hexSecondChar(char ch) {
	return hexChar((*reinterpret_cast<uchar*>(&ch)) & 0x0F);
}

char hexFirstChar(char ch) {
	return hexChar((*reinterpret_cast<uchar*>(&ch)) >> 4);
}

QString stringToEncodedString(const QString &str) {
	QString result, lineBreak = "\\\n";
	result.reserve(str.size() * 8);
	bool writingHexEscapedCharacters = false, startOnNewLine = false;
	int lastCutSize = 0;
	auto utf = str.toUtf8();
	for (auto ch : utf) {
		if (result.size() - lastCutSize > 80) {
			startOnNewLine = true;
			result.append(lineBreak);
			lastCutSize = result.size();
		}
		if (ch == '\n') {
			writingHexEscapedCharacters = false;
			result.append("\\n");
		} else if (ch == '\t') {
			writingHexEscapedCharacters = false;
			result.append("\\t");
		} else if (ch == '"' || ch == '\\') {
			writingHexEscapedCharacters = false;
			result.append('\\').append(ch);
		} else if (ch < 32 || static_cast<uchar>(ch) > 127) {
			writingHexEscapedCharacters = true;
			result.append("\\x").append(hexFirstChar(ch)).append(hexSecondChar(ch));
		} else {
			if (writingHexEscapedCharacters) {
				writingHexEscapedCharacters = false;
				result.append("\"\"");
			}
			result.append(ch);
		}
	}
	return '"' + (startOnNewLine ? lineBreak : QString()) + result + '"';
}

QString stringToEncodedString(const std::string &str) {
	return stringToEncodedString(QString::fromStdString(str));
}

QString stringToBinaryArray(const std::string &str) {
	QStringList rows, chars;
	chars.reserve(13);
	rows.reserve(1 + (str.size() / 13));
	for (uchar ch : str) {
		if (chars.size() > 12) {
			rows.push_back(chars.join(", "));
			chars.clear();
		}
		chars.push_back(QString("0x") + hexFirstChar(ch) + hexSecondChar(ch));
	}
	if (!chars.isEmpty()) {
		rows.push_back(chars.join(", "));
	}
	return QString("{") + ((rows.size() > 1) ? '\n' : ' ') + rows.join(",\n") + " }";
}

} // namespace

Generator::Generator(const LangPack &langpack, const QString &destBasePath, const common::ProjectInfo &project)
: langpack_(langpack)
, basePath_(destBasePath)
, baseName_(QFileInfo(basePath_).baseName())
, project_(project) {
}

bool Generator::writeHeader() {
	header_ = std::make_unique<common::CppFile>(basePath_ + ".h", project_);
	header_->include("lang/lang_tag.h").newline().pushNamespace("Lang").stream() << "\
\n\
constexpr auto kTagsCount = " << langpack_.tags.size() << ";\n\
\n";

	header_->popNamespace().newline();
	auto index = 0;
	for (auto &tag : langpack_.tags) {
		header_->stream() << "enum lngtag_" << tag.tag << " { lt_" << tag.tag << " = " << index++ << " };\n";
	}
	header_->stream() << "\
\n\
enum LangKey : int {\n";
	for (auto &entry : langpack_.entries) {
		header_->stream() << "\t" << getFullKey(entry) << ",\n";
	}
	header_->stream() << "\
\n\
	kLangKeysCount,\n\
};\n\
\n\
QString lang(LangKey key);\n\
\n";

	for (auto &entry : langpack_.entries) {
		auto isPlural = !entry.keyBase.isEmpty();
		auto &key = entry.key;
		auto genericParams = QStringList();
		auto params = QStringList();
		auto applyTags = QStringList();
		auto plural = QString();
		auto nonPluralTagFound = false;
		for (auto &tagData : entry.tags) {
			auto &tag = tagData.tag;
			auto isPluralTag = isPlural && (tag == kPluralTag);
			genericParams.push_back("lngtag_" + tag + ", " + (isPluralTag ? "float64 " : "const ResultString &") + tag + "__val");
			params.push_back("lngtag_" + tag + ", " + (isPluralTag ? "float64 " : "const QString &") + tag + "__val");
			if (isPluralTag) {
				plural = "\tauto plural = Lang::Plural(" + key + ", " + kPluralTag + "__val);\n";
				applyTags.push_back("\tresult = Lang::ReplaceTag<ResultString>::Call(std::move(result), lt_" + tag + ", Lang::StartReplacements<ResultString>::Call(std::move(plural.replacement)));\n");
			} else {
				nonPluralTagFound = true;
				applyTags.push_back("\tresult = Lang::ReplaceTag<ResultString>::Call(std::move(result), lt_" + tag + ", " + tag + "__val);\n");
			}
		}
		if (!entry.tags.empty() && (!isPlural || key == ComputePluralKey(entry.keyBase, 0))) {
			auto initialString = isPlural ? ("std::move(plural.string)") : ("lang(" + getFullKey(entry) + ")");
			header_->stream() << "\
template <typename ResultString>\n\
inline ResultString " << (isPlural ? entry.keyBase : key) << "__generic(" << genericParams.join(QString(", ")) << ") {\n\
" << plural << "\
	auto result = Lang::StartReplacements<ResultString>::Call(" << initialString << ");\n\
" << applyTags.join(QString()) << "\
	return result;\n\
}\n\
constexpr auto " << (isPlural ? entry.keyBase : key) << " = &" << (isPlural ? entry.keyBase : key) << "__generic<QString>;\n\
\n";
		}
	}

	header_->pushNamespace("Lang").stream() << "\
\n\
const char *GetKeyName(LangKey key);\n\
ushort GetTagIndex(QLatin1String tag);\n\
LangKey GetKeyIndex(QLatin1String key);\n\
bool IsTagReplaced(LangKey key, ushort tag);\n\
QString GetOriginalValue(LangKey key);\n\
\n";

	return header_->finalize();
}

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(basePath_ + ".cpp", project_);

	source_->include("lang/lang_keys.h").pushNamespace("Lang").pushNamespace().stream() << "\
const char *KeyNames[kLangKeysCount] = {\n\
\n";
	for (auto &entry : langpack_.entries) {
		source_->stream() << "\"" << entry.key << "\",\n";
	}
	source_->stream() << "\
\n\
};\n\
\n\
QChar DefaultData[] = {";
	auto count = 0;
	auto fulllength = 0;
	for (auto &entry : langpack_.entries) {
		for (auto ch : entry.value) {
			if (fulllength > 0) source_->stream() << ",";
			if (!count++) {
				source_->stream() << "\n";
			} else {
				if (count == 12) {
					count = 0;
				}
				source_->stream() << " ";
			}
			source_->stream() << "0x" << QString::number(ch.unicode(), 16);
			++fulllength;
		}
	}
	source_->stream() << " };\n\
\n\
int Offsets[] = {";
	count = 0;
	auto offset = 0;
	auto writeOffset = [this, &count, &offset] {
		if (offset > 0) source_->stream() << ",";
		if (!count++) {
			source_->stream() << "\n";
		} else {
			if (count == 12) {
				count = 0;
			}
			source_->stream() << " ";
		}
		source_->stream() << offset;
	};
	for (auto &entry : langpack_.entries) {
		writeOffset();
		offset += entry.value.size();
	}
	writeOffset();
	source_->stream() << " };\n";
	source_->popNamespace().stream() << "\
\n\
const char *GetKeyName(LangKey key) {\n\
	return (key < 0 || key >= kLangKeysCount) ? \"\" : KeyNames[key];\n\
}\n\
\n\
ushort GetTagIndex(QLatin1String tag) {\n\
	auto size = tag.size();\n\
	auto data = tag.data();\n";

	auto tagsSet = std::set<QString, std::greater<QString>>();
	for (auto &tag : langpack_.tags) {
		tagsSet.insert(tag.tag);
	}

	writeSetSearch(tagsSet, [](const QString &tag) {
		return "lt_" + tag;
	}, "kTagsCount");

	source_->stream() << "\
}\n\
\n\
LangKey GetKeyIndex(QLatin1String key) {\n\
	auto size = key.size();\n\
	auto data = key.data();\n";

	auto taggedKeys = std::map<QString, QString>();
	auto keysSet = std::set<QString, std::greater<QString>>();
	for (auto &entry : langpack_.entries) {
		if (!entry.keyBase.isEmpty()) {
			for (auto i = 0; i != kPluralPartCount; ++i) {
				auto keyName = entry.keyBase + '#' + kPluralParts[i];
				taggedKeys.emplace(keyName, ComputePluralKey(entry.keyBase, i));
				keysSet.insert(keyName);
			}
		} else {
			auto full = getFullKey(entry);
			if (full != entry.key) {
				taggedKeys.emplace(entry.key, full);
			}
			keysSet.insert(entry.key);
		}
	}

	writeSetSearch(keysSet, [&taggedKeys](const QString &key) {
		auto it = taggedKeys.find(key);
		return (it != taggedKeys.end()) ? it->second : key;
	}, "kLangKeysCount");

	source_->stream() << "\
}\n\
\n\
bool IsTagReplaced(LangKey key, ushort tag) {\n\
	switch (key) {\n";

	auto lastWrittenPluralEntry = QString();
	for (auto &entry : langpack_.entries) {
		if (entry.tags.empty()) {
			continue;
		}
		if (!entry.keyBase.isEmpty()) {
			if (entry.keyBase == lastWrittenPluralEntry) {
				continue;
			}
			lastWrittenPluralEntry = entry.keyBase;
			for (auto i = 0; i != kPluralPartCount; ++i) {
				source_->stream() << "\
	case " << ComputePluralKey(entry.keyBase, i) << ":" << ((i + 1 == kPluralPartCount) ? " {" : "") << "\n";
			}
		} else {
			source_->stream() << "\
	case " << getFullKey(entry) << ": {\n";
		}
		source_->stream() << "\
		switch (tag) {\n";
		for (auto &tag : entry.tags) {
			source_->stream() << "\
		case lt_" << tag.tag << ":\n";
		}
		source_->stream() << "\
			return true;\n\
		}\n\
	} break;\n";
	}

	source_->stream() << "\
	}\
\n\
	return false;\n\
}\n\
\n\
QString GetOriginalValue(LangKey key) {\n\
	Expects(key >= 0 && key < kLangKeysCount);\n\
	auto offset = Offsets[key];\n\
	return QString::fromRawData(DefaultData + offset, Offsets[key + 1] - offset);\n\
}\n\
\n";

	return source_->finalize();
}

template <typename ComputeResult>
void Generator::writeSetSearch(const std::set<QString, std::greater<QString>> &set, ComputeResult computeResult, const QString &invalidResult) {
	auto tabs = [](int size) {
		return QString(size, '\t');
	};

	enum class UsedCheckType {
		Switch,
		If,
		UpcomingIf,
	};
	auto checkTypes = QVector<UsedCheckType>();
	auto checkLengthHistory = QVector<int>(1, 0);
	auto chars = QString();
	auto tabsUsed = 1;

	// Returns true if at least one check was finished.
	auto finishChecksTillKey = [this, &chars, &checkTypes, &checkLengthHistory, &tabsUsed, tabs](const QString &key) {
		auto result = false;
		while (!chars.isEmpty() && key.midRef(0, chars.size()) != chars) {
			result = true;

			auto wasType = checkTypes.back();
			chars.resize(chars.size() - 1);
			checkTypes.pop_back();
			checkLengthHistory.pop_back();
			if (wasType == UsedCheckType::Switch || wasType == UsedCheckType::If) {
				--tabsUsed;
				if (wasType == UsedCheckType::Switch) {
					source_->stream() << tabs(tabsUsed) << "break;\n";
				}
				if ((!chars.isEmpty() && key.midRef(0, chars.size()) != chars) || key == chars) {
					source_->stream() << tabs(tabsUsed) << "}\n";
				}
			}
		}
		return result;
	};

	// Check if we can use "if" for a check on "charIndex" in "it" (otherwise only "switch")
	auto canUseIfForCheck = [](auto it, auto end, int charIndex) {
		auto key = *it;
		auto i = it;
		auto keyStart = key.mid(0, charIndex);
		for (++i; i != end; ++i) {
			auto nextKey = *i;
			if (nextKey.mid(0, charIndex) != keyStart) {
				return true;
			} else if (nextKey.size() > charIndex && nextKey[charIndex] != key[charIndex]) {
				return false;
			}
		}
		return true;
	};

	auto countMinimalLength = [](auto it, auto end, int charIndex) {
		auto key = *it;
		auto i = it;
		auto keyStart = key.mid(0, charIndex);
		auto result = key.size();
		for (++i; i != end; ++i) {
			auto nextKey = *i;
			if (nextKey.mid(0, charIndex) != keyStart) {
				break;
			} else if (nextKey.size() > charIndex && result > nextKey.size()) {
				result = nextKey.size();
			}
		}
		return result;
	};

	for (auto i = set.begin(), e = set.end(); i != e; ++i) {
		// If we use just "auto" here and "name" becomes mutable,
		// the operator[] will return QCharRef instead of QChar,
		// and "auto ch = name[index]" will behave like "auto &ch =",
		// if you assign something to "ch" after that you'll change "name" (!)
		const auto name = *i;

		auto weContinueOldSwitch = finishChecksTillKey(name);
		while (chars.size() != name.size()) {
			auto checking = chars.size();
			auto partialKey = name.mid(0, checking);

			auto keyChar = name[checking];
			auto usedIfForCheckCount = 0;
			auto minimalLengthCheck = countMinimalLength(i, e, checking);
			for (; checking + usedIfForCheckCount != name.size(); ++usedIfForCheckCount) {
				if (!canUseIfForCheck(i, e, checking + usedIfForCheckCount)
					|| countMinimalLength(i, e, checking + usedIfForCheckCount) != minimalLengthCheck) {
					break;
				}
			}
			auto usedIfForCheck = !weContinueOldSwitch && (usedIfForCheckCount > 0);
			auto checkLengthCondition = QString();
			if (weContinueOldSwitch) {
				weContinueOldSwitch = false;
			} else {
				checkLengthCondition = (minimalLengthCheck > checkLengthHistory.back()) ? ("size >= " + QString::number(minimalLengthCheck)) : QString();
				if (!usedIfForCheck) {
					source_->stream() << tabs(tabsUsed) << (checkLengthCondition.isEmpty() ? QString() : ("if (" + checkLengthCondition + ") ")) << "switch (data[" << checking << "]) {\n";
				}
			}
			if (usedIfForCheck) {
				auto conditions = QStringList();
				if (usedIfForCheckCount > 1) {
					conditions.push_back("!memcmp(data + " + QString::number(checking) + ", \"" + name.mid(checking, usedIfForCheckCount) + "\", " + QString::number(usedIfForCheckCount) + ")");
				} else {
					conditions.push_back("data[" + QString::number(checking) + "] == '" + keyChar + "'");
				}
				if (!checkLengthCondition.isEmpty()) {
					conditions.push_front(checkLengthCondition);
				}
				source_->stream() << tabs(tabsUsed) << "if (" << conditions.join(" && ") << ") {\n";
				checkTypes.push_back(UsedCheckType::If);
				for (auto i = 1; i != usedIfForCheckCount; ++i) {
					checkTypes.push_back(UsedCheckType::UpcomingIf);
					chars.push_back(keyChar);
					checkLengthHistory.push_back(qMax(minimalLengthCheck, checkLengthHistory.back()));
					keyChar = name[checking + i];
				}
			} else {
				source_->stream() << tabs(tabsUsed) << "case '" << keyChar << "':\n";
				checkTypes.push_back(UsedCheckType::Switch);
			}
			++tabsUsed;
			chars.push_back(keyChar);
			checkLengthHistory.push_back(qMax(minimalLengthCheck, checkLengthHistory.back()));
		}
		source_->stream() << tabs(tabsUsed) << "return (size == " << chars.size() << ") ? " << computeResult(name) << " : " << invalidResult << ";\n";
	}
	finishChecksTillKey(QString());

	source_->stream() << "\
\n\
	return " << invalidResult << ";\n";
}

QString Generator::getFullKey(const LangPack::Entry &entry) {
	if (!entry.keyBase.isEmpty() || entry.tags.empty()) {
		return entry.key;
	}
	return entry.key + "__tagged";
}

} // namespace lang
} // namespace codegen
