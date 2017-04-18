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
#include "lang/lang_cloud_manager.h"

#include "lang/lang_instance.h"
#include "mtproto/mtp_instance.h"
#include "storage/localstorage.h"
#include "messenger.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"

namespace Lang {

CloudManager::CloudManager(Instance &langpack, gsl::not_null<MTP::Instance*> mtproto) : MTP::Sender(mtproto)
, _langpack(langpack) {
	requestLangPackDifference();
}

void CloudManager::requestLangPackDifference() {
	if (_langpack.isCustom() || _langPackRequestId) {
		return;
	}

	auto version = _langpack.version();
	if (version > 0) {
		_langPackRequestId = request(MTPlangpack_GetDifference(MTP_int(version))).done([this](const MTPLangPackDifference &result) {
			_langPackRequestId = 0;
			applyLangPackDifference(result);
		}).fail([this](const RPCError &error) {
			_langPackRequestId = 0;
		}).send();
	} else {
		_langPackRequestId = request(MTPlangpack_GetLangPack()).done([this](const MTPLangPackDifference &result) {
			_langPackRequestId = 0;
			applyLangPackDifference(result);
		}).fail([this](const RPCError &error) {
			_langPackRequestId = 0;
		}).send();
	}
}

void CloudManager::applyLangPackDifference(const MTPLangPackDifference &difference) {
	Expects(difference.type() == mtpc_langPackDifference);
	if (_langpack.isCustom()) {
		return;
	}

	auto &langpack = difference.c_langPackDifference();
	auto langpackId = qs(langpack.vlang_code);
	if (needToApplyLangPack(langpackId)) {
		applyLangPackData(langpack);
	} else if (_langpack.id().isEmpty()) {
		_offerSwitchToId = langpackId;
		if (langpack.vfrom_version.v == 0) {
			_offerSwitchToData = std::make_unique<MTPLangPackDifference>(difference);
		} else {
			_offerSwitchToData.reset();
		}
		offerSwitchLangPack();
	} else {
		LOG(("Lang Warning: Ignoring update for '%1' because our language is '%2'").arg(langpackId).arg(_langpack.id()));
	}
}

void CloudManager::requestLanguageList() {
	_languagesRequestId = request(MTPlangpack_GetLanguages()).done([this](const MTPVector<MTPLangPackLanguage> &result) {
		auto languages = Languages();
		for_const (auto &langData, result.v) {
			t_assert(langData.type() == mtpc_langPackLanguage);
			auto &language = langData.c_langPackLanguage();
			languages.push_back({ qs(language.vlang_code), qs(language.vname) });
		}
		if (_languages != languages) {
			_languages = languages;
			_languagesChanged.notify();
		}
		_languagesRequestId = 0;
	}).fail([this](const RPCError &error) {
		_languagesRequestId = 0;
	}).send();
}

bool CloudManager::needToApplyLangPack(const QString &id) {
	auto currentId = _langpack.id();
	if (currentId == id) {
		return true;
	} else if (currentId.isEmpty() && id == DefaultLanguageId()) {
		return true;
	}
	return false;
}

void CloudManager::offerSwitchLangPack() {
	Expects(!_offerSwitchToId.isEmpty());
	Expects(_offerSwitchToId != DefaultLanguageId());

	if (!showOfferSwitchBox()) {
		subscribe(languageListChanged(), [this] {
			showOfferSwitchBox();
		});
		requestLanguageList();
	}
}

QString CloudManager::findOfferedLanguageName() {
	for_const (auto &language, _languages) {
		if (language.id == _offerSwitchToId) {
			return language.name;
		}
	}
	return QString();
}

bool CloudManager::showOfferSwitchBox() {
	auto name = findOfferedLanguageName();
	if (name.isEmpty()) {
		return false;
	}

	Ui::show(Box<ConfirmBox>("Do you want to switch your language to " + name + "? You can always change your language in Settings.", "Change", lang(lng_cancel), [this] {
		Ui::hideLayer();
		if (_offerSwitchToId.isEmpty()) {
			return;
		}
		if (_offerSwitchToData) {
			t_assert(_offerSwitchToData->type() == mtpc_langPackDifference);
			applyLangPackData(base::take(_offerSwitchToData)->c_langPackDifference());
		} else {
			switchToLanguage(_offerSwitchToId);
		}
	}, [this] {
		Ui::hideLayer();
		changeIdAndReInitConnection(DefaultLanguageId());
		Local::writeLangPack();
	}));
	return true;
}

void CloudManager::applyLangPackData(const MTPDlangPackDifference &data) {
	switchLangPackId(qs(data.vlang_code));
	if (_langpack.version() < data.vfrom_version.v) {
		requestLangPackDifference();
	} else if (!data.vstrings.v.isEmpty()) {
		_langpack.applyDifference(data);
		Local::writeLangPack();
		auto fullLangPackUpdated = (data.vfrom_version.v == 0);
		if (fullLangPackUpdated) {
			_langpack.updated().notify();
		}
	} else {
		LOG(("Lang Info: Up to date."));
	}
}

void CloudManager::switchToLanguage(const QString &id) {
	switchLangPackId(id);
	requestLangPackDifference();
}

void CloudManager::switchLangPackId(const QString &id) {
	auto currentId = _langpack.id();
	auto notChanged = (currentId == id) || (currentId.isEmpty() && id == DefaultLanguageId());
	if (!notChanged) {
		changeIdAndReInitConnection(id);
	}
}

void CloudManager::changeIdAndReInitConnection(const QString &id) {
	_langpack.switchToId(id);

	auto mtproto = requestMTP();
	mtproto->reInitConnection(mtproto->mainDcId());
}

CloudManager &CurrentCloudManager() {
	auto result = Messenger::Instance().langCloudManager();
	t_assert(result != nullptr);
	return *result;
}

} // namespace Lang
