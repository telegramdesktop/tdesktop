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
#include "lang/lang_instance.h"

#include "messenger.h"
#include "lang/lang_file_parser.h"
#include "platform/platform_specific.h"
#include "storage/serialize_common.h"
#include "storage/localstorage.h"

namespace Lang {
namespace {

constexpr auto kDefaultLanguage = str_const("en");
constexpr auto kLangValuesLimit = 20000;

class ValueParser {
public:
	ValueParser(const QByteArray &key, LangKey keyIndex, const QByteArray &value);

	QString takeResult() {
		Expects(!_failed);
		return std::move(_result);
	}
	std::map<LangKey, QString> takePluralValues() {
		Expects(!_failed);
		return std::move(_plural);
	}

	bool parse();

private:
	void appendToResult(const char *nextBegin);
	void appendToPlural(const char *nextBegin);
	bool feedPluralValue();
	bool logError(const QString &text);
	bool readTag();

	const QByteArray &_key;
	LangKey _keyIndex = kLangKeysCount;

	QLatin1String _currentTag;
	ushort _currentTagIndex = 0;
	QString _currentTagReplacer;

	QString _pluralValue;
	int _pluralIndex = 0;
	bool _pluralNumericFound = false;

	bool _failed = true;

	const char *_begin, *_ch, *_end;

	QString _result;
	std::map<LangKey, QString> _plural;
	OrderedSet<ushort> _tagsUsed;

};

ValueParser::ValueParser(const QByteArray &key, LangKey keyIndex, const QByteArray &value)
: _key(key)
, _keyIndex(keyIndex)
, _begin(value.constData())
, _ch(_begin)
, _end(_begin + value.size()) {
}

void ValueParser::appendToResult(const char *nextBegin) {
	if (_ch > _begin) _result.append(QString::fromUtf8(_begin, _ch - _begin));
	_begin = nextBegin;
}

void ValueParser::appendToPlural(const char *nextBegin) {
	if (_ch > _begin) _pluralValue.append(QString::fromUtf8(_begin, _ch - _begin));
	_begin = nextBegin;
}

bool ValueParser::feedPluralValue() {
	appendToPlural(_ch + 1);

	if (_pluralIndex >= kTagsPluralVariants) {
		return logError("Too many values inside counted tag");
	}
	auto pluralKeyIndex = GetSubkeyIndex(_keyIndex, _currentTagIndex, _pluralIndex);
	if (pluralKeyIndex == kLangKeysCount) {
		return logError("Unexpected counted tag");
	} else {
		_plural.emplace(pluralKeyIndex, _pluralValue);
	}
	++_pluralIndex;
	_pluralValue = QString();
	_pluralNumericFound = false;
	return true;
};

bool ValueParser::logError(const QString &text) {
	_failed = true;
	auto loggedKey = (_currentTag.size() > 0) ? (_key + QString(':') + _currentTag) : QString(_key);
	LOG(("Lang Error: %1 (key '%2')").arg(text).arg(loggedKey));
	return false;
}

bool ValueParser::readTag() {
	auto tagStart = _ch;
	auto isTagChar = [](QChar ch) {
		if (ch >= 'a' && ch <= 'z') {
			return true;
		} else if (ch >= 'A' && ch <= 'z') {
			return true;
		} else if (ch >= '0' && ch <= '9') {
			return true;
		}
		return (ch == '_');
	};
	while (_ch != _end && isTagChar(*_ch)) {
		++_ch;
	}
	if (_ch == tagStart) {
		return logError("Expected tag name");
	}

	_currentTag = QLatin1String(tagStart, _ch - tagStart);
	if (_ch == _end || (*_ch != '}' && *_ch != ':')) {
		return logError("Expected '}' or ':' after tag name");
	}

	_currentTagIndex = GetTagIndex(_currentTag);
	if (_currentTagIndex == kTagsCount) {
		return logError("Unknown tag");
	}
	if (!IsTagReplaced(_keyIndex, _currentTagIndex)) {
		return logError("Unexpected tag");
	}
	if (_tagsUsed.contains(_currentTagIndex)) {
		return logError("Repeated tag");
	}
	_tagsUsed.insert(_currentTagIndex);

	if (_currentTagReplacer.isEmpty()) {
		_currentTagReplacer = QString(4, TextCommand);
		_currentTagReplacer[1] = TextCommandLangTag;
	}
	_currentTagReplacer[2] = QChar(0x0020 + _currentTagIndex);

	return true;
}

bool ValueParser::parse() {
	_failed = false;
	_result.reserve(_end - _begin);
	for (; _ch != _end; ++_ch) {
		if (*_ch == '{') {
			appendToResult(_ch);

			++_ch;
			if (!readTag()) {
				return false;
			}

			_result.append(_currentTagReplacer);

			_begin = _ch + 1;
			if (*_ch == ':') {
				_pluralIndex = 0;
				while (_ch != _end && *_ch != '}') {
					if (*_ch == '|') {
						if (!feedPluralValue()) {
							return false;
						}
					} else if (*_ch == '\\') {
						if (_ch + 1 >= _end) {
							return logError("Unexpected end of file inside counted tag");
						}
						if (*(_ch + 1) == '{' || *(_ch + 1) == '#' || *(_ch + 1) == '}') {
							appendToPlural(_ch + 1);
						}
					} else if (*_ch == '{') {
						return logError("Unexpected tag inside counted tag");
					} else if (*_ch == '#') {
						if (_pluralNumericFound) {
							return logError("Replacement '#' double used inside counted tag");
						}
						_pluralNumericFound = true;
						appendToPlural(_ch + 1);
						_pluralValue.append(_currentTagReplacer);
					}
					++_ch;
				}
				if (_ch == _end) {
					return logError("Unexpected end of value inside counted tag");
				}
				if (!feedPluralValue()) {
					return false;
				}
			}
			_currentTag = QLatin1String();
		}
	}
	appendToResult(_end);
	return true;
}

} // namespace

void Instance::fillDefaults() {
	_values.clear();
	_values.reserve(kLangKeysCount);
	for (auto i = 0; i != kLangKeysCount; ++i) {
		_values.emplace_back(GetOriginalValue(LangKey(i)));
	}
	_id = str_const_toString(kDefaultLanguage);
	_legacyId = kLegacyDefaultLanguage;
}

QString Instance::cloudLangCode() const {
	if (isCustom() || id().isEmpty()) {
		if (_systemLanguage.isEmpty()) {
			_systemLanguage = Platform::SystemLanguage();
			if (_systemLanguage.isEmpty()) {
				_systemLanguage = str_const_toString(kDefaultLanguage);
			}
		}
		return _systemLanguage;
	}
	return id();
}

QByteArray Instance::serialize() const {
	auto size = Serialize::stringSize(_id);
	size += sizeof(qint32); // version
	size += Serialize::stringSize(_customFilePathAbsolute) + Serialize::stringSize(_customFilePathRelative);
	size += Serialize::bytearraySize(_customFileContent);
	size += sizeof(qint32); // _nonDefaultValues.size()
	for (auto &nonDefault : _nonDefaultValues) {
		size += Serialize::bytearraySize(nonDefault.first) + Serialize::bytearraySize(nonDefault.second);
	}

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << _id << qint32(_version);
		stream << _customFilePathAbsolute << _customFilePathRelative << _customFileContent;
		stream << qint32(_nonDefaultValues.size());
		for (auto &nonDefault : _nonDefaultValues) {
			stream << nonDefault.first << nonDefault.second;
		}
	}
	return result;
}

void Instance::fillFromSerialized(const QByteArray &data) {
	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);
	QString id;
	qint32 version = 0;
	QString customFilePathAbsolute, customFilePathRelative;
	QByteArray customFileContent;
	qint32 nonDefaultValuesCount = 0;
	stream >> id >> version;
	stream >> customFilePathAbsolute >> customFilePathRelative >> customFileContent;
	stream >> nonDefaultValuesCount;
	if (stream.status() != QDataStream::Ok) {
		LOG(("Lang Error: Could not read data from serialized langpack."));
		return;
	}
	if (nonDefaultValuesCount > kLangValuesLimit) {
		LOG(("Lang Error: Values count limit exceeded: %1").arg(nonDefaultValuesCount));
		return;
	}

	if (!customFilePathAbsolute.isEmpty()) {
		auto currentCustomFileContent = Lang::FileParser::ReadFile(customFilePathAbsolute, customFilePathRelative);
		if (!currentCustomFileContent.isEmpty() && currentCustomFileContent != customFileContent) {
			loadFromCustomContent(customFilePathAbsolute, customFilePathRelative, currentCustomFileContent);
			Local::writeLangPack();
			return;
		}
	}

	std::vector<QByteArray> nonDefaultStrings;
	nonDefaultStrings.reserve(2 * nonDefaultValuesCount);
	for (auto i = 0; i != nonDefaultValuesCount; ++i) {
		QByteArray key, value;
		stream >> key >> value;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Lang Error: Could not read data from serialized langpack."));
			return;
		}

		nonDefaultStrings.push_back(key);
		nonDefaultStrings.push_back(value);
	}

	_id = id;
	_version = version;
	_customFilePathAbsolute = customFilePathAbsolute;
	_customFilePathRelative = customFilePathRelative;
	_customFileContent = customFileContent;
	LOG(("Lang Info: Loaded cached, keys: %1").arg(nonDefaultValuesCount));
	for (auto i = 0, count = nonDefaultValuesCount * 2; i != count; i += 2) {
		applyValue(nonDefaultStrings[i], nonDefaultStrings[i + 1]);
	}
}

void Instance::loadFromContent(const QByteArray &content) {
	Lang::FileParser loader(content, [this](QLatin1String key, const QByteArray &value) {
		applyValue(QByteArray(key.data(), key.size()), value);
	});
	if (!loader.errors().isEmpty()) {
		LOG(("Lang load errors: %1").arg(loader.errors()));
	} else if (!loader.warnings().isEmpty()) {
		LOG(("Lang load warnings: %1").arg(loader.warnings()));
	}
}

void Instance::loadFromCustomContent(const QString &absolutePath, const QString &relativePath, const QByteArray &content) {
	_id = qsl("custom");
	_version = 0;
	_customFilePathAbsolute = absolutePath;
	_customFilePathRelative = relativePath;
	_customFileContent = content;
	loadFromContent(_customFileContent);
}

void Instance::fillFromCustomFile(const QString &filePath) {
	auto absolutePath = QFileInfo(filePath).absoluteFilePath();
	auto relativePath = QDir().relativeFilePath(filePath);
	auto content = Lang::FileParser::ReadFile(absolutePath, relativePath);
	if (!content.isEmpty()) {
		loadFromCustomContent(absolutePath, relativePath, content);
	}
}

void Instance::fillFromLegacy(int legacyId, const QString &legacyPath) {
	if (legacyId == kLegacyDefaultLanguage) {
		_legacyId = legacyId;
		_id = str_const_toString(kLegacyLanguages[legacyId]);
	} else if (legacyId == kLegacyCustomLanguage) {
		auto absolutePath = QFileInfo(legacyPath).absoluteFilePath();
		auto relativePath = QDir().relativeFilePath(absolutePath);
		auto content = Lang::FileParser::ReadFile(absolutePath, relativePath);
		if (!content.isEmpty()) {
			loadFromCustomContent(absolutePath, relativePath, content);
		}
	} else if (legacyId > kLegacyDefaultLanguage && legacyId < base::array_size(kLegacyLanguages)) {
		auto languageId = str_const_toString(kLegacyLanguages[_legacyId]);
		auto resourcePath = qsl(":/langs/lang_") + languageId + qsl(".strings");
		auto content = Lang::FileParser::ReadFile(resourcePath, resourcePath);
		if (!content.isEmpty()) {
			_legacyId = legacyId;
			_id = languageId;
			_version = 0;
			loadFromContent(content);
		}
	}
}

void Instance::applyDifference(const MTPDlangPackDifference &difference) {
	Expects(qs(difference.vlang_code) == _id);
	Expects(difference.vfrom_version.v <= _version);

	_version = difference.vversion.v;
	for_const (auto &mtpString, difference.vstrings.v) {
		switch (mtpString.type()) {
		case mtpc_langPackString: {
			auto &string = mtpString.c_langPackString();
			applyValue(qba(string.vkey), qba(string.vvalue));
		} break;

		case mtpc_langPackStringPluralized: {
			auto &string = mtpString.c_langPackStringPluralized();
			auto key = qba(string.vkey);
			applyValue(key + "#zero", string.has_zero_value() ? qba(string.vzero_value) : QByteArray());
			applyValue(key + "#one", string.has_one_value() ? qba(string.vone_value) : QByteArray());
			applyValue(key + "#two", string.has_two_value() ? qba(string.vtwo_value) : QByteArray());
			applyValue(key + "#few", string.has_few_value() ? qba(string.vfew_value) : QByteArray());
			applyValue(key + "#many", string.has_many_value() ? qba(string.vmany_value) : QByteArray());
			applyValue(key + "#other", qba(string.vother_value));
		} break;

		case mtpc_langPackStringDeleted: {
			auto &string = mtpString.c_langPackStringDeleted();
			auto key = qba(string.vkey);
			resetValue(key);
			for (auto plural : { "#zero", "#one", "#two", "#few", "#many", "#other" }) {
				resetValue(key + plural);
			}
		} break;

		default: Unexpected("LangPack string type in applyUpdate().");
		}
	}
}

void Instance::resetValue(const QByteArray &key) {
	_nonDefaultValues.erase(key);

	auto keyIndex = GetKeyIndex(QLatin1String(key));
	if (keyIndex != kLangKeysCount) {
		_values[keyIndex] = GetOriginalValue(keyIndex);
	}
}

void Instance::applyValue(const QByteArray &key, const QByteArray &value) {
	_nonDefaultValues[key] = value;

	auto pluralValues = std::map<ushort, QString>();
	auto keyIndex = GetKeyIndex(QLatin1String(key));
	if (keyIndex == kLangKeysCount) {
		LOG(("Lang Error: Unknown key '%1'").arg(QString::fromLatin1(key)));
		return;
	}

	ValueParser parser(key, keyIndex, value);
	if (!parser.parse()) {
		return;
	}

	_values[keyIndex] = parser.takeResult();
	for (auto &plural : parser.takePluralValues()) {
		_values[plural.first] = plural.second;
	}
}

Instance &Current() {
	return Messenger::Instance().langpack();
}

} // namespace Lang
