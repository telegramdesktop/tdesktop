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

#include "mtproto/sender.h"
#include "base/weak_unique_ptr.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace Lang {

class Instance;

class CloudManager : public base::enable_weak_from_this, private MTP::Sender, private base::Subscriber {
public:
	CloudManager(Instance &langpack, not_null<MTP::Instance*> mtproto);

	struct Language {
		QString id;
		QString name;
		QString nativeName;
	};
	using Languages = QVector<Language>;

	void requestLanguageList();
	Languages languageList() const {
		return _languages;
	}
	base::Observable<void> &languageListChanged() {
		return _languagesChanged;
	}
	void requestLangPackDifference();
	void applyLangPackDifference(const MTPLangPackDifference &difference);

	void resetToDefault();
	void switchToLanguage(QString id);
	void switchToTestLanguage();
	void setSuggestedLanguage(const QString &langCode);
	QString suggestedLanguage() const {
		return _suggestedLanguage;
	}
	base::Observable<void> &firstLanguageSuggestion() {
		return _firstLanguageSuggestion;
	}

private:
	bool canApplyWithoutRestart(const QString &id) const;
	void performSwitchToCustom();
	void performSwitch(const QString &id);
	void performSwitchAndRestart(const QString &id);
	void offerSwitchLangPack();
	bool showOfferSwitchBox();
	QString findOfferedLanguageName();

	bool needToApplyLangPack(const QString &id);
	void applyLangPackData(const MTPDlangPackDifference &data);
	void switchLangPackId(const QString &id);
	void changeIdAndReInitConnection(const QString &id);

	Instance &_langpack;
	Languages _languages;
	base::Observable<void> _languagesChanged;
	mtpRequestId _langPackRequestId = 0;
	mtpRequestId _languagesRequestId = 0;

	QString _offerSwitchToId;
	bool _restartAfterSwitch = false;

	QString _suggestedLanguage;
	bool _languageWasSuggested = false;
	base::Observable<void> _firstLanguageSuggestion;

	mtpRequestId _switchingToLanguageRequest = 0;

};

inline bool operator==(const CloudManager::Language &a, const CloudManager::Language &b) {
	return (a.id == b.id) && (a.name == b.name);
}

inline bool operator!=(const CloudManager::Language &a, const CloudManager::Language &b) {
	return !(a == b);
}

CloudManager &CurrentCloudManager();

} // namespace Lang
