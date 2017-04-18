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

namespace Lang {

CloudManager::CloudManager(Instance &langpack, gsl::not_null<MTP::Instance*> mtproto) : MTP::Sender(mtproto)
, _langpack(langpack) {
	requestLangPackDifference();
}

void CloudManager::requestLangPackDifference() {
	auto &langpack = Lang::Current();
	if (langpack.isCustom() || _langPackRequestId) {
		return;
	}

	auto version = langpack.version();
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
	auto &current = Lang::Current();
	if (current.isCustom()) {
		return;
	}

	auto &langpack = difference.c_langPackDifference();
	switchLangPackId(qs(langpack.vlang_code));
	if (current.version() < langpack.vfrom_version.v) {
		requestLangPackDifference();
	} else if (!langpack.vstrings.v.isEmpty()) {
		current.applyDifference(langpack);
		Local::writeLangPack();
		auto fullLangPackUpdated = (langpack.vfrom_version.v == 0);
		if (fullLangPackUpdated) {
			Lang::Current().updated().notify();
		}
	} else {
		LOG(("Lang Info: Up to date."));
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

void CloudManager::switchToLanguage(const QString &id) {
	switchLangPackId(id);
	requestLangPackDifference();
}

void CloudManager::switchLangPackId(const QString &id) {
	auto &current = Lang::Current();
	if (current.id() != id) {
		current.switchToId(id);

		auto mtproto = requestMTP();
		mtproto->reInitConnection(mtproto->mainDcId());
	}
}

CloudManager &CurrentCloudManager() {
	auto result = Messenger::Instance().langCloudManager();
	t_assert(result != nullptr);
	return *result;
}

} // namespace Lang
