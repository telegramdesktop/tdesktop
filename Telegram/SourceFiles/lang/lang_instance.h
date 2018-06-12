/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include "lang_auto.h"
#include "base/weak_ptr.h"

namespace Lang {

constexpr auto kLegacyLanguageNone = -2;
constexpr auto kLegacyCustomLanguage = -1;
constexpr auto kLegacyDefaultLanguage = 0;

constexpr str_const kLegacyLanguages[] = {
	"en",
	"it",
	"es",
	"de",
	"nl",
	"pt_BR",
	"ko",
};

inline QString ConvertLegacyLanguageId(const QString &languageId) {
	return languageId.toLower().replace('_', '-');
}

QString DefaultLanguageId();

class Instance;
Instance &Current();

rpl::producer<QString> Viewer(LangKey key);

class Instance {
public:
	Instance() {
		fillDefaults();
	}
	void switchToId(const QString &id);
	void switchToCustomFile(const QString &filePath);

	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;
	Instance(Instance &&other) = default;
	Instance &operator=(Instance &&other) = default;

	QString systemLangCode() const;
	QString cloudLangCode() const;

	QString id() const {
		return _id;
	}
	bool isCustom() const {
		return (_id == qstr("custom") || _id == qstr("TEST_X") || _id == qstr("TEST_0"));
	}
	int version() const {
		return _version;
	}

	QByteArray serialize() const;
	void fillFromSerialized(const QByteArray &data);
	void fillFromLegacy(int legacyId, const QString &legacyPath);

	void applyDifference(const MTPDlangPackDifference &difference);
	static std::map<LangKey, QString> ParseStrings(const MTPVector<MTPLangPackString> &strings);
	base::Observable<void> &updated() {
		return _updated;
	}

	QString getValue(LangKey key) const {
		Expects(key >= 0 && key < kLangKeysCount);
		Expects(_values.size() == kLangKeysCount);
		return _values[key];
	}
	bool isNonDefaultPlural(LangKey key) const {
		Expects(key >= 0 && key < kLangKeysCount);
		Expects(_nonDefaultSet.size() == kLangKeysCount);
		return _nonDefaultSet[key]
			|| _nonDefaultSet[key + 1]
			|| _nonDefaultSet[key + 2]
			|| _nonDefaultSet[key + 3]
			|| _nonDefaultSet[key + 4]
			|| _nonDefaultSet[key + 5];
	}

private:
	// SetCallback takes two QByteArrays: key, value.
	// It is called for all key-value pairs in string.
	// ResetCallback takes one QByteArray: key.
	template <typename SetCallback, typename ResetCallback>
	static void HandleString(const MTPLangPackString &mtpString, SetCallback setCallback, ResetCallback resetCallback);

	// Writes each key-value pair in the result container.
	template <typename Result>
	static LangKey ParseKeyValue(const QByteArray &key, const QByteArray &value, Result &result);

	void applyValue(const QByteArray &key, const QByteArray &value);
	void resetValue(const QByteArray &key);
	void reset();
	void fillDefaults();
	void fillFromCustomFile(const QString &filePath);
	void loadFromContent(const QByteArray &content);
	void loadFromCustomContent(const QString &absolutePath, const QString &relativePath, const QByteArray &content);
	void updatePluralRules();

	QString _id;
	int _legacyId = kLegacyLanguageNone;
	QString _customFilePathAbsolute;
	QString _customFilePathRelative;
	QByteArray _customFileContent;
	int _version = 0;
	base::Observable<void> _updated;

	mutable QString _systemLanguage;

	std::vector<QString> _values;
	std::vector<uchar> _nonDefaultSet;
	std::map<QByteArray, QByteArray> _nonDefaultValues;

};

} // namespace Lang
