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
#pragma once

#include "lang_auto.h"

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

class Instance {
public:
	Instance() {
		fillDefaults();
	}
	struct CreateFromIdTag {};
	Instance(const QString &id, CreateFromIdTag) {
		fillDefaults();
		_id = id;
	}
	struct CreateFromCustomFileTag {};
	Instance(const QString &filePath, CreateFromCustomFileTag) {
		fillDefaults();
		fillFromCustomFile(filePath);
	}
	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;
	Instance(Instance &&other) = default;
	Instance &operator=(Instance &&other) = default;

	QString id() const {
		return _id;
	}
	bool isCustom() const {
		return id() == qstr("custom");
	}
	int version() const {
		return _version;
	}
	QString cloudLangCode() const;

	QByteArray serialize() const;
	void fillFromSerialized(const QByteArray &data);
	void fillFromLegacy(int legacyId, const QString &legacyPath);

	void applyDifference(const MTPDlangPackDifference &difference);

	QString getValue(LangKey key) {
		Expects(key >= 0 && key < kLangKeysCount);
		Expects(_values.size() == kLangKeysCount);
		return _values[key];
	}

private:
	void applyValue(const QByteArray &key, const QByteArray &value);
	void resetValue(const QByteArray &key);
	void fillDefaults();
	void fillFromCustomFile(const QString &filePath);
	void loadFromContent(const QByteArray &content);
	void loadFromCustomContent(const QString &absolutePath, const QString &relativePath, const QByteArray &content);

	QString _id;
	int _legacyId = kLegacyLanguageNone;
	QString _customFilePathAbsolute;
	QString _customFilePathRelative;
	QByteArray _customFileContent;
	int _version = 0;
	mutable QString _systemLanguage;

	std::vector<QString> _values;
	std::map<QByteArray, QByteArray> _nonDefaultValues;

};

Instance &Current();

} // namespace Lang
