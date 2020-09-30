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
Instance &GetInstance();

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
	rpl::producer<QString> idChanges() const;
	QString baseId() const;
	QString name() const;
	QString nativeName() const;
	QString id(Pack pack) const;
	bool isCustom() const;
	int version(Pack pack) const;

	QByteArray serialize() const;
	void fillFromSerialized(const QByteArray &data, int dataAppVersion);

	void applyDifference(
		Pack pack,
		const MTPDlangPackDifference &difference);
	static std::map<ushort, QString> ParseStrings(
		const MTPVector<MTPLangPackString> &strings);

	[[nodiscard]] rpl::producer<> updated() const {
		return _updated.events();
	}

	QString getValue(ushort key) const {
		Expects(key < _values.size());

		return _values[key];
	}
	QString getNonDefaultValue(const QByteArray &key) const;
	bool isNonDefaultPlural(ushort key) const {
		Expects(key + 5 < _nonDefaultSet.size());

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
	rpl::event_stream<QString> _idChanges;
	QString _name, _nativeName;
	QString _customFilePathAbsolute;
	QString _customFilePathRelative;
	QByteArray _customFileContent;
	int _version = 0;
	rpl::event_stream<> _updated;

	mutable QString _systemLanguage;

	std::vector<QString> _values;
	std::vector<uchar> _nonDefaultSet;
	std::map<QByteArray, QByteArray> _nonDefaultValues;

	std::unique_ptr<Instance> _base;

};

namespace details {

QString Current(ushort key);
rpl::producer<QString> Value(ushort key);

} // namespace details
} // namespace Lang
