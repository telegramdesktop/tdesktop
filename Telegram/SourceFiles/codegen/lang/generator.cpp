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
	header_->include("lang/lang_tag.h").include("lang/lang_values.h").newline();

	writeHeaderForwardDeclarations();
	writeHeaderTagTypes();
	writeHeaderInterface();
	writeHeaderReactiveInterface();

	return header_->finalize();
}

void Generator::writeHeaderForwardDeclarations() {
	header_->pushNamespace("Lang").stream() << "\
\n\
inline constexpr auto kTagsCount = ushort(" << langpack_.tags.size() << ");\n\
inline constexpr auto kKeysCount = ushort(" << langpack_.entries.size() << ");\n\
\n";
	header_->popNamespace().newline();
}

void Generator::writeHeaderTagTypes() {
	auto index = 0;
	for (auto &tag : langpack_.tags) {
		if (tag.tag == kPluralTags[0]) {
			auto elements = QStringList();
			header_->stream()
				<< "enum lngtag_" << tag.tag << " : int { ";
			for (auto i = 0; i != kPluralTags.size(); ++i) {
				elements.push_back("lt_" + kPluralTags[i] + " = " + QString::number(index + i * 1000));
			}
			header_->stream() << elements.join(", ") << " };\n";
			++index;
		} else {
			header_->stream() << "enum lngtag_" << tag.tag << " : int { lt_" << tag.tag << " = " << index++ << " };\n";
		}
	}
	header_->newline();
}

void Generator::writeHeaderInterface() {
	header_->pushNamespace("Lang").stream() << "\
\n\
ushort GetTagIndex(QLatin1String tag);\n\
ushort GetKeyIndex(QLatin1String key);\n\
bool IsTagReplaced(ushort key, ushort tag);\n\
QString GetOriginalValue(ushort key);\n\
\n";
	writeHeaderTagValueLookup();
	header_->popNamespace().newline();
}

void Generator::writeHeaderTagValueLookup() {
	header_->pushNamespace("details").stream() << "\
\n\
template <typename Tag>\n\
struct TagData;\n\
\n\
template <typename Tag>\n\
inline constexpr ushort TagValue() {\n\
	return TagData<Tag>::value;\n\
}\n\
\n";

	for (auto &tag : langpack_.tags) {
		header_->stream() << "template <> struct TagData<lngtag_" << tag.tag << "> : std::integral_constant<ushort, ushort(lt_" << tag.tag << ")> {};\n";
	}

	header_->newline().popNamespace();
}

void Generator::writeHeaderReactiveInterface() {
	header_->pushNamespace("tr");

	writeHeaderProducersInterface();
	writeHeaderProducersInstances();

	header_->popNamespace().newline();
}

void Generator::writeHeaderProducersInterface() {
	header_->pushNamespace("details").stream() << "\
\n\
struct Identity {\n\
	QString operator()(const QString &value) const {\n\
		return value;\n\
	}\n\
};\n\
\n";

	header_->popNamespace().newline();
	header_->stream() << "\
struct now_t {\n\
};\n\
\n\
inline constexpr now_t now{};\n\
\n\
inline auto to_count() {\n\
	return rpl::map([](auto value) {\n\
		return float64(value);\n\
	});\n\
}\n\
\n\
template <typename P>\n\
using S = std::decay_t<decltype(std::declval<P>()(QString()))>;\n\
\n\
template <typename ...Tags>\n\
struct phrase;\n\
\n";
	std::set<QString> producersDeclared;
	for (auto &entry : langpack_.entries) {
		const auto isPlural = !entry.keyBase.isEmpty();
		const auto &key = entry.key;
		auto tags = QStringList();
		auto producerArgs = QStringList();
		auto currentArgs = QStringList();
		auto values = QStringList();
		values.push_back("base");
		values.push_back("std::move(p)");
		for (auto &tagData : entry.tags) {
			const auto &tag = tagData.tag;
			const auto isPluralTag = isPlural && (tag == kPluralTags[0]);
			tags.push_back("lngtag_" + tag);
			const auto type1 = "lngtag_" + tag;
			const auto arg1 = type1 + (isPluralTag ? " type" : "");
			const auto producerType2 = (isPluralTag ? "rpl::producer<float64> " : "rpl::producer<S<P>> ");
			const auto producerArg2 = producerType2 + tag + "__val";
			const auto currentType2 = (isPluralTag ? "float64 " : "const S<P> &");
			const auto currentArg2 = currentType2 + tag + "__val";
			producerArgs.push_back(arg1 + ", " + producerArg2);
			currentArgs.push_back(arg1 + ", " + currentArg2);
			if (isPluralTag) {
				values.push_back("type");
			}
			values.push_back(tag + "__val");
		}
		producerArgs.push_back("P p = P()");
		currentArgs.push_back("P p = P()");
		if (!producersDeclared.emplace(tags.join(',')).second) {
			continue;
		}
		header_->stream() << "\
template <>\n\
struct phrase<" << tags.join(", ") << "> {\n\
	template <typename P = details::Identity>\n\
	rpl::producer<S<P>> operator()(" << producerArgs.join(", ") << ") const {\n\
		return ::Lang::details::Producer<" << tags.join(", ") << ">::template Combine(" << values.join(", ") << ");\n\
	}\n\
\n\
	template <typename P = details::Identity>\n\
	S<P> operator()(now_t, " << currentArgs.join(", ") << ") const {\n\
		return ::Lang::details::Producer<" << tags.join(", ") << ">::template Current(" << values.join(", ") << ");\n\
	}\n\
\n\
	ushort base;\n\
};\n\
\n";
	}
}

void Generator::writeHeaderProducersInstances() {
	auto index = 0;
	for (auto &entry : langpack_.entries) {
		const auto isPlural = !entry.keyBase.isEmpty();
		const auto &key = entry.key;
		auto tags = QStringList();
		for (auto &tagData : entry.tags) {
			const auto &tag = tagData.tag;
			tags.push_back("lngtag_" + tag);
		}
		if (!isPlural || key == ComputePluralKey(entry.keyBase, 0)) {
			header_->stream() << "\
inline constexpr phrase<" << tags.join(", ") << "> " << (isPlural ? entry.keyBase : key) << "{ ushort(" << index << ") };\n";
		}
		++index;
	}
	header_->newline();
}

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(basePath_ + ".cpp", project_);

	source_->include("lang/lang_keys.h").pushNamespace("Lang").pushNamespace();

	source_->stream() << "\
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
ushort GetTagIndex(QLatin1String tag) {\n\
	auto size = tag.size();\n\
	auto data = tag.data();\n";

	auto tagsSet = std::set<QString, std::greater<>>();
	for (auto &tag : langpack_.tags) {
		tagsSet.insert(tag.tag);
	}

	writeSetSearch(tagsSet, [](const QString &tag) {
		return "ushort(lt_" + tag + ")";
	}, "kTagsCount");

	source_->stream() << "\
}\n\
\n\
ushort GetKeyIndex(QLatin1String key) {\n\
	auto size = key.size();\n\
	auto data = key.data();\n";

	auto index = 0;
	auto indices = std::map<QString, QString>();
	for (auto &entry : langpack_.entries) {
		indices.emplace(getFullKey(entry), QString::number(index++));
	}
	const auto indexOfKey = [&](const QString &full) {
		const auto i = indices.find(full);
		if (i == indices.end()) {
			return QString();
		}
		return i->second;
	};

	auto taggedKeys = std::map<QString, QString>();
	auto keysSet = std::set<QString, std::greater<>>();
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

	writeSetSearch(keysSet, [&](const QString &key) {
		auto it = taggedKeys.find(key);
		const auto name = (it != taggedKeys.end()) ? it->second : key;
		return indexOfKey(name);
	}, "kKeysCount");
	header_->popNamespace().newline();

	source_->stream() << "\
}\n\
\n\
bool IsTagReplaced(ushort key, ushort tag) {\n\
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
	case " << indexOfKey(ComputePluralKey(entry.keyBase, i)) << ":" << ((i + 1 == kPluralPartCount) ? " {" : "") << "\n";
			}
		} else {
			source_->stream() << "\
	case " << indexOfKey(getFullKey(entry)) << ": {\n";
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
QString GetOriginalValue(ushort key) {\n\
	Expects(key < kKeysCount);\n\
\n\
	const auto offset = Offsets[key];\n\
	return QString::fromRawData(DefaultData + offset, Offsets[key + 1] - offset);\n\
}\n\
\n";

	return source_->finalize();
}

template <typename ComputeResult>
void Generator::writeSetSearch(const std::set<QString, std::greater<>> &set, ComputeResult computeResult, const QString &invalidResult) {
	auto tabs = [](int size) {
		return QString(size, '\t');
	};

	enum class UsedCheckType {
		Switch,
		If,
		UpcomingIf,
	};
	auto checkTypes = QVector<UsedCheckType>();
	auto checkLengthHistory = QVector<int>();
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
			if (wasType == UsedCheckType::Switch || wasType == UsedCheckType::If) {
				--tabsUsed;
				if (wasType == UsedCheckType::Switch) {
					source_->stream() << tabs(tabsUsed) << "break;\n";
				}
				if ((!chars.isEmpty() && key.midRef(0, chars.size()) != chars) || key == chars) {
					source_->stream() << tabs(tabsUsed) << "}\n";
					checkLengthHistory.pop_back();
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
			const auto checkedLength = checkLengthHistory.isEmpty()
				? 0
				: checkLengthHistory.back();
			const auto requiredLength = qMax(
				minimalLengthCheck,
				checkedLength);
			auto checkLengthCondition = QString();
			if (weContinueOldSwitch) {
				weContinueOldSwitch = false;
			} else {
				checkLengthCondition = (requiredLength > checkedLength) ? ("size >= " + QString::number(requiredLength)) : QString();
				if (!usedIfForCheck) {
					source_->stream() << tabs(tabsUsed) << (checkLengthCondition.isEmpty() ? QString() : ("if (" + checkLengthCondition + ") ")) << "switch (data[" << checking << "]) {\n";
					checkLengthHistory.push_back(requiredLength);
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
				checkLengthHistory.push_back(requiredLength);
				checkTypes.push_back(UsedCheckType::If);
				for (auto i = 1; i != usedIfForCheckCount; ++i) {
					checkTypes.push_back(UsedCheckType::UpcomingIf);
					chars.push_back(keyChar);
					keyChar = name[checking + i];
				}
			} else {
				source_->stream() << tabs(tabsUsed) << "case '" << keyChar << "':\n";
				checkTypes.push_back(UsedCheckType::Switch);
			}
			++tabsUsed;
			chars.push_back(keyChar);
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
