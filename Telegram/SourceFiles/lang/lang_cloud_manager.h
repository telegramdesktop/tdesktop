/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/weak_ptr.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace Lang {

class Instance;
enum class Pack;

struct Language {
	QString id;
	QString pluralId;
	QString baseId;
	QString name;
	QString nativeName;
};
Language ParseLanguage(const MTPLangPackLanguage &data);

class CloudManager : public base::has_weak_ptr, private MTP::Sender, private base::Subscriber {
public:
	CloudManager(Instance &langpack, not_null<MTP::Instance*> mtproto);

	using Languages = QVector<Language>;

	void requestLanguageList();
	Languages languageList() const {
		return _languages;
	}
	base::Observable<void> &languageListChanged() {
		return _languagesChanged;
	}
	void requestLangPackDifference(const QString &langId);
	void applyLangPackDifference(const MTPLangPackDifference &difference);

	void resetToDefault();
	void switchWithWarning(const QString &id);
	void switchToLanguage(
		const QString &id,
		const QString &pluralId = QString(),
		const QString &baseId = QString());
	void switchToTestLanguage();
	void setSuggestedLanguage(const QString &langCode);
	QString suggestedLanguage() const {
		return _suggestedLanguage;
	}
	base::Observable<void> &firstLanguageSuggestion() {
		return _firstLanguageSuggestion;
	}

private:
	mtpRequestId &packRequestId(Pack pack);
	mtpRequestId packRequestId(Pack pack) const;
	Pack packTypeFromId(const QString &id) const;
	void requestLangPackDifference(Pack pack);
	bool canApplyWithoutRestart(const QString &id) const;
	void performSwitchToCustom();
	void performSwitch(
		const QString &id,
		const QString &pluralId = QString(),
		const QString &baseId = QString());
	void performSwitchAndRestart(
		const QString &id,
		const QString &pluralId = QString(),
		const QString &baseId = QString());
	void restartAfterSwitch();
	void offerSwitchLangPack();
	bool showOfferSwitchBox();
	QString findOfferedLanguageName();

	void applyLangPackData(Pack pack, const MTPDlangPackDifference &data);
	void switchLangPackId(
		const QString &id,
		const QString &pluralId,
		const QString &baseId);
	void changeIdAndReInitConnection(
		const QString &id,
		const QString &pluralId = QString(),
		const QString &baseId = QString());

	Instance &_langpack;
	Languages _languages;
	base::Observable<void> _languagesChanged;
	mtpRequestId _langPackRequestId = 0;
	mtpRequestId _langPackBaseRequestId = 0;
	mtpRequestId _languagesRequestId = 0;

	QString _offerSwitchToId;
	bool _restartAfterSwitch = false;

	QString _suggestedLanguage;
	bool _languageWasSuggested = false;
	base::Observable<void> _firstLanguageSuggestion;

	mtpRequestId _switchingToLanguageRequest = 0;

};

inline bool operator==(const Language &a, const Language &b) {
	return (a.id == b.id) && (a.name == b.name);
}

inline bool operator!=(const Language &a, const Language &b) {
	return !(a == b);
}

CloudManager &CurrentCloudManager();

} // namespace Lang
