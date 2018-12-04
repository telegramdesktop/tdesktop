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

struct Language {
	QString id;
	QString pluralId;
	QString baseId;
	QString name;
	QString nativeName;
};

inline bool operator==(const Language &a, const Language &b) {
	return (a.id == b.id) && (a.name == b.name);
}

inline bool operator!=(const Language &a, const Language &b) {
	return !(a == b);
}

QString DefaultLanguageId();
QString LanguageIdOrDefault(const QString &id);
QString CloudLangPackName();
QString CustomLanguageId();
Language DefaultLanguage();

class Instance;
Instance &Current();

rpl::producer<QString> Viewer(LangKey key);

enum class Pack {
	None,
	Current,
	Base,
};

class Instance {
	struct PrivateTag;

public:
	Instance();
	Instance(not_null<Instance*> derived, const PrivateTag &);

	void switchToId(const Language &language);
	void switchToCustomFile(const QString &filePath);

	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;
	Instance(Instance &&other) = default;
	Instance &operator=(Instance &&other) = default;

	QString systemLangCode() const;
	QString langPackName() const;
	QString cloudLangCode(Pack pack) const;
	QString id() const;
	QString baseId() const;
	QString name() const;
	QString nativeName() const;
	QString id(Pack pack) const;
	bool isCustom() const;
	int version(Pack pack) const;

	QByteArray serialize() const;
	void fillFromSerialized(const QByteArray &data, int dataAppVersion);
	void fillFromLegacy(int legacyId, const QString &legacyPath);

	void applyDifference(
		Pack pack,
		const MTPDlangPackDifference &difference);
	static std::map<LangKey, QString> ParseStrings(
		const MTPVector<MTPLangPackString> &strings);
	base::Observable<void> &updated() {
		return _updated;
	}

	QString getValue(LangKey key) const {
		Expects(key >= 0 && key < _values.size());

		return _values[key];
	}
	QString getNonDefaultValue(const QByteArray &key) const;
	bool isNonDefaultPlural(LangKey key) const {
		Expects(key >= 0 && key + 5 < _nonDefaultSet.size());

		return _nonDefaultSet[key]
			|| _nonDefaultSet[key + 1]
			|| _nonDefaultSet[key + 2]
			|| _nonDefaultSet[key + 3]
			|| _nonDefaultSet[key + 4]
			|| _nonDefaultSet[key + 5]
			|| (_base && _base->isNonDefaultPlural(key));
	}

private:
	void setBaseId(const QString &baseId, const QString &pluralId);

	void applyDifferenceToMe(const MTPDlangPackDifference &difference);
	void applyValue(const QByteArray &key, const QByteArray &value);
	void resetValue(const QByteArray &key);
	void reset(const Language &language);
	void fillFromCustomContent(
		const QString &absolutePath,
		const QString &relativePath,
		const QByteArray &content);
	bool loadFromCustomFile(const QString &filePath);
	void loadFromContent(const QByteArray &content);
	void loadFromCustomContent(
		const QString &absolutePath,
		const QString &relativePath,
		const QByteArray &content);
	void updatePluralRules();

	Instance *_derived = nullptr;

	QString _id, _pluralId;
	QString _name, _nativeName;
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

	std::unique_ptr<Instance> _base;

};

} // namespace Lang
