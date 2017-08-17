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
#include "lang/lang_file_parser.h"
#include "core/file_utilities.h"

namespace Lang {

CloudManager::CloudManager(Instance &langpack, not_null<MTP::Instance*> mtproto) : MTP::Sender()
, _langpack(langpack) {
	requestLangPackDifference();
}

void CloudManager::requestLangPackDifference() {
	request(_langPackRequestId).cancel();
	if (_langpack.isCustom()) {
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
		_langPackRequestId = request(MTPlangpack_GetLangPack(MTP_string(_langpack.cloudLangCode()))).done([this](const MTPLangPackDifference &result) {
			_langPackRequestId = 0;
			applyLangPackDifference(result);
		}).fail([this](const RPCError &error) {
			_langPackRequestId = 0;
		}).send();
	}
}

void CloudManager::setSuggestedLanguage(const QString &langCode) {
	if (!langCode.isEmpty()
		&& langCode != Lang::DefaultLanguageId()) {
		_suggestedLanguage = langCode;
	} else {
		_suggestedLanguage = QString();
	}

	if (!_languageWasSuggested) {
		_languageWasSuggested = true;
		_firstLanguageSuggestion.notify();

		if (AuthSession::Exists() && _langpack.id().isEmpty() && !_suggestedLanguage.isEmpty()) {
			auto isLegacy = [](const QString &languageId) {
				for (auto &legacyString : kLegacyLanguages) {
					auto legacyId = str_const_toString(legacyString);
					if (ConvertLegacyLanguageId(legacyId) == languageId) {
						return true;
					}
				}
				return false;
			};

			// The old available languages (de/it/nl/ko/es/pt_BR) won't be
			// suggested anyway, because everyone saw the suggestion in intro.
			if (!isLegacy(_suggestedLanguage)) {
				_offerSwitchToId = _suggestedLanguage;
				offerSwitchLangPack();
			}
		}
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
		if (_restartAfterSwitch) {
			App::restart();
		}
	} else {
		LOG(("Lang Warning: Ignoring update for '%1' because our language is '%2'").arg(langpackId).arg(_langpack.id()));
	}
}

void CloudManager::requestLanguageList() {
	_languagesRequestId = request(MTPlangpack_GetLanguages()).done([this](const MTPVector<MTPLangPackLanguage> &result) {
		auto languages = Languages();
		for_const (auto &langData, result.v) {
			Assert(langData.type() == mtpc_langPackLanguage);
			auto &language = langData.c_langPackLanguage();
			languages.push_back({ qs(language.vlang_code), qs(language.vname), qs(language.vnative_name) });
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
		performSwitchAndRestart(_offerSwitchToId);
	}, [this] {
		Ui::hideLayer();
		changeIdAndReInitConnection(DefaultLanguageId());
		Local::writeLangPack();
	}), KeepOtherLayers);
	return true;
}

void CloudManager::applyLangPackData(const MTPDlangPackDifference &data) {
	switchLangPackId(qs(data.vlang_code));
	if (_langpack.version() < data.vfrom_version.v) {
		requestLangPackDifference();
	} else if (!data.vstrings.v.isEmpty()) {
		_langpack.applyDifference(data);
		Local::writeLangPack();
	} else {
		LOG(("Lang Info: Up to date."));
	}
}

bool CloudManager::canApplyWithoutRestart(const QString &id) const {
	if (id == qstr("TEST_X") || id == qstr("TEST_0")) {
		return true;
	}

	// We don't support instant language switch if the auth session exists :(
	return !AuthSession::Exists();
}

void CloudManager::resetToDefault() {
	performSwitch(DefaultLanguageId());
}

void CloudManager::switchToLanguage(QString id) {
	if (id.isEmpty()) {
		id = DefaultLanguageId();
	}
	if (_langpack.id() == id && id != qstr("custom")) {
		return;
	}

	request(_switchingToLanguageRequest).cancel();
	if (id == qstr("custom")) {
		performSwitchToCustom();
	} else if (canApplyWithoutRestart(id)) {
		performSwitch(id);
	} else {
		QVector<MTPstring> keys;
		keys.reserve(3);
		keys.push_back(MTP_string("lng_sure_save_language"));
		keys.push_back(MTP_string("lng_box_ok"));
		keys.push_back(MTP_string("lng_cancel"));
		_switchingToLanguageRequest = request(MTPlangpack_GetStrings(MTP_string(id), MTP_vector<MTPstring>(std::move(keys)))).done([this, id](const MTPVector<MTPLangPackString> &result) {
			auto values = Instance::ParseStrings(result);
			auto getValue = [&values](LangKey key) {
				auto it = values.find(key);
				return (it == values.cend()) ? GetOriginalValue(key) : it->second;
			};
			auto text = getValue(lng_sure_save_language);
			auto save = getValue(lng_box_ok);
			auto cancel = getValue(lng_cancel);
			Ui::show(Box<ConfirmBox>(text, save, cancel, [this, id] {
				performSwitchAndRestart(id);
			}), KeepOtherLayers);
		}).send();
	}
}

void CloudManager::performSwitchToCustom() {
	auto filter = qsl("Language files (*.strings)");
	auto title = qsl("Choose language .strings file");
	FileDialog::GetOpenPath(title, filter, [weak = base::make_weak_unique(this)](const FileDialog::OpenResult &result) {
		if (!weak || result.paths.isEmpty()) {
			return;
		}

		auto filePath = result.paths.front();
		Lang::FileParser loader(filePath, { lng_sure_save_language, lng_box_ok, lng_cancel });
		if (loader.errors().isEmpty()) {
			weak->request(weak->_switchingToLanguageRequest).cancel();
			if (weak->canApplyWithoutRestart(qsl("custom"))) {
				weak->_langpack.switchToCustomFile(filePath);
			} else {
				auto values = loader.found();
				auto getValue = [&values](LangKey key) {
					auto it = values.find(key);
					return (it == values.cend()) ? GetOriginalValue(key) : it.value();
				};
				auto text = getValue(lng_sure_save_language);
				auto save = getValue(lng_box_ok);
				auto cancel = getValue(lng_cancel);
				Ui::show(Box<ConfirmBox>(text, save, cancel, [weak, filePath] {
					weak->_langpack.switchToCustomFile(filePath);
					App::restart();
				}), KeepOtherLayers);
			}
		} else {
			Ui::show(Box<InformBox>("Custom lang failed :(\n\nError: " + loader.errors()), KeepOtherLayers);
		}
	});
}

void CloudManager::switchToTestLanguage() {
	auto testLanguageId = (_langpack.id() == qstr("TEST_X")) ? qsl("TEST_0") : qsl("TEST_X");
	performSwitch(testLanguageId);
}

void CloudManager::performSwitch(const QString &id) {
	_restartAfterSwitch = false;
	switchLangPackId(id);
	requestLangPackDifference();
}

void CloudManager::performSwitchAndRestart(const QString &id) {
	performSwitch(id);
	if (_langPackRequestId) {
		_restartAfterSwitch = true;
	} else {
		App::restart();
	}
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
	Assert(result != nullptr);
	return *result;
}

} // namespace Lang
