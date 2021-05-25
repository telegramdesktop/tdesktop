/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_cloud_manager.h"

#include "lang/lang_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_text_entity.h"
#include "mtproto/mtp_instance.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "boxes/confirm_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "app.h"
#include "styles/style_layers.h"

namespace Lang {
namespace {

class ConfirmSwitchBox : public Ui::BoxContent {
public:
	ConfirmSwitchBox(
		QWidget*,
		const MTPDlangPackLanguage &data,
		Fn<void()> apply);

protected:
	void prepare() override;

private:
	QString _name;
	int _percent = 0;
	bool _official = false;
	QString _editLink;
	Fn<void()> _apply;

};

class NotReadyBox : public Ui::BoxContent {
public:
	NotReadyBox(
		QWidget*,
		const MTPDlangPackLanguage &data);

protected:
	void prepare() override;

private:
	QString _name;
	QString _editLink;

};

ConfirmSwitchBox::ConfirmSwitchBox(
	QWidget*,
	const MTPDlangPackLanguage &data,
	Fn<void()> apply)
: _name(qs(data.vnative_name()))
, _percent(data.vtranslated_count().v * 100 / data.vstrings_count().v)
, _official(data.is_official())
, _editLink(qs(data.vtranslations_url()))
, _apply(std::move(apply)) {
}

void ConfirmSwitchBox::prepare() {
	setTitle(tr::lng_language_switch_title());

	auto text = (_official
		? tr::lng_language_switch_about_official
		: tr::lng_language_switch_about_unofficial)(
			lt_lang_name,
			rpl::single(Ui::Text::Bold(_name)),
			lt_percent,
			rpl::single(Ui::Text::Bold(QString::number(_percent))),
			lt_link,
			tr::lng_language_switch_link() | Ui::Text::ToLink(_editLink),
			Ui::Text::WithEntities);
	const auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setLinksTrusted();

	addButton(tr::lng_language_switch_apply(), [=] {
		const auto apply = _apply;
		closeBox();
		apply();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWideWidth);
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, lifetime());
}

NotReadyBox::NotReadyBox(
	QWidget*,
	const MTPDlangPackLanguage &data)
: _name(qs(data.vnative_name()))
, _editLink(qs(data.vtranslations_url())) {
}

void NotReadyBox::prepare() {
	setTitle(tr::lng_language_not_ready_title());

	auto text = tr::lng_language_not_ready_about(
		lt_lang_name,
		rpl::single(_name) | Ui::Text::ToWithEntities(),
		lt_link,
		tr::lng_language_not_ready_link() | Ui::Text::ToLink(_editLink),
		Ui::Text::WithEntities);
	const auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setLinksTrusted();

	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWidth);
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, lifetime());
}

} // namespace

Language ParseLanguage(const MTPLangPackLanguage &data) {
	return data.match([](const MTPDlangPackLanguage &data) {
		return Language{
			qs(data.vlang_code()),
			qs(data.vplural_code()),
			qs(data.vbase_lang_code().value_or_empty()),
			qs(data.vname()),
			qs(data.vnative_name())
		};
	});
}

CloudManager::CloudManager(Instance &langpack)
: _langpack(langpack) {
	Core::App().domain().activeValue(
	) | rpl::map([=](Main::Account *account) {
		if (!account) {
			_api.reset();
		}
		return account
			? account->mtpValue()
			: rpl::never<not_null<MTP::Instance*>>();
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		resendRequests();
	}, _lifetime);
}

Pack CloudManager::packTypeFromId(const QString &id) const {
	if (id == LanguageIdOrDefault(_langpack.id())) {
		return Pack::Current;
	} else if (id == _langpack.baseId()) {
		return Pack::Base;
	}
	return Pack::None;
}

rpl::producer<> CloudManager::languageListChanged() const {
	return _languageListChanged.events();
}

rpl::producer<> CloudManager::firstLanguageSuggestion() const {
	return _firstLanguageSuggestion.events();
}

void CloudManager::requestLangPackDifference(const QString &langId) {
	Expects(!langId.isEmpty());

	if (langId == LanguageIdOrDefault(_langpack.id())) {
		requestLangPackDifference(Pack::Current);
	} else {
		requestLangPackDifference(Pack::Base);
	}
}

mtpRequestId &CloudManager::packRequestId(Pack pack) {
	return (pack != Pack::Base)
		? _langPackRequestId
		: _langPackBaseRequestId;
}

mtpRequestId CloudManager::packRequestId(Pack pack) const {
	return (pack != Pack::Base)
		? _langPackRequestId
		: _langPackBaseRequestId;
}

void CloudManager::requestLangPackDifference(Pack pack) {
	if (!_api) {
		return;
	}
	_api->request(base::take(packRequestId(pack))).cancel();
	if (_langpack.isCustom()) {
		return;
	}

	const auto version = _langpack.version(pack);
	const auto code = _langpack.cloudLangCode(pack);
	if (code.isEmpty()) {
		return;
	}
	if (version > 0) {
		packRequestId(pack) = _api->request(MTPlangpack_GetDifference(
			MTP_string(CloudLangPackName()),
			MTP_string(code),
			MTP_int(version)
		)).done([=](const MTPLangPackDifference &result) {
			packRequestId(pack) = 0;
			applyLangPackDifference(result);
		}).fail([=](const MTP::Error &error) {
			packRequestId(pack) = 0;
		}).send();
	} else {
		packRequestId(pack) = _api->request(MTPlangpack_GetLangPack(
			MTP_string(CloudLangPackName()),
			MTP_string(code)
		)).done([=](const MTPLangPackDifference &result) {
			packRequestId(pack) = 0;
			applyLangPackDifference(result);
		}).fail([=](const MTP::Error &error) {
			packRequestId(pack) = 0;
		}).send();
	}
}

void CloudManager::setSuggestedLanguage(const QString &langCode) {
	if (Lang::LanguageIdOrDefault(langCode) != Lang::DefaultLanguageId()) {
		_suggestedLanguage = langCode;
	} else {
		_suggestedLanguage = QString();
	}

	if (!_languageWasSuggested) {
		_languageWasSuggested = true;
		_firstLanguageSuggestion.fire({});

		if (Core::App().offerLegacyLangPackSwitch()
			&& _langpack.id().isEmpty()
			&& !_suggestedLanguage.isEmpty()) {
			_offerSwitchToId = _suggestedLanguage;
			offerSwitchLangPack();
		}
	}
}

void CloudManager::setCurrentVersions(int version, int baseVersion) {
	const auto check = [&](Pack pack, int version) {
		if (version > _langpack.version(pack) && !packRequestId(pack)) {
			requestLangPackDifference(pack);
		}
	};
	check(Pack::Current, version);
	check(Pack::Base, baseVersion);
}

void CloudManager::applyLangPackDifference(
		const MTPLangPackDifference &difference) {
	Expects(difference.type() == mtpc_langPackDifference);

	if (_langpack.isCustom()) {
		return;
	}

	const auto &langpack = difference.c_langPackDifference();
	const auto langpackId = qs(langpack.vlang_code());
	const auto pack = packTypeFromId(langpackId);
	if (pack != Pack::None) {
		applyLangPackData(pack, langpack);
		if (_restartAfterSwitch) {
			restartAfterSwitch();
		}
	} else {
		LOG(("Lang Warning: "
			"Ignoring update for '%1' because our language is '%2'").arg(
			langpackId,
			_langpack.id()));
	}
}

void CloudManager::requestLanguageList() {
	if (!_api) {
		_languagesRequestId = -1;
		return;
	}
	_api->request(base::take(_languagesRequestId)).cancel();
	_languagesRequestId = _api->request(MTPlangpack_GetLanguages(
		MTP_string(CloudLangPackName())
	)).done([=](const MTPVector<MTPLangPackLanguage> &result) {
		auto languages = Languages();
		for (const auto &language : result.v) {
			languages.push_back(ParseLanguage(language));
		}
		if (_languages != languages) {
			_languages = languages;
			_languageListChanged.fire({});
		}
		_languagesRequestId = 0;
	}).fail([=](const MTP::Error &error) {
		_languagesRequestId = 0;
	}).send();
}

void CloudManager::offerSwitchLangPack() {
	Expects(!_offerSwitchToId.isEmpty());
	Expects(_offerSwitchToId != DefaultLanguageId());

	if (!showOfferSwitchBox()) {
		languageListChanged(
		) | rpl::start_with_next([=] {
			showOfferSwitchBox();
		}, _lifetime);
		requestLanguageList();
	}
}

Language CloudManager::findOfferedLanguage() const {
	for (const auto &language : _languages) {
		if (language.id == _offerSwitchToId) {
			return language;
		}
	}
	return {};
}

bool CloudManager::showOfferSwitchBox() {
	const auto language = findOfferedLanguage();
	if (language.id.isEmpty()) {
		return false;
	}

	const auto confirm = [=] {
		Ui::hideLayer();
		if (_offerSwitchToId.isEmpty()) {
			return;
		}
		performSwitchAndRestart(language);
	};
	const auto cancel = [=] {
		Ui::hideLayer();
		changeIdAndReInitConnection(DefaultLanguage());
		Local::writeLangPack();
	};
	Ui::show(
		Box<ConfirmBox>(
			"Do you want to switch your language to "
			+ language.nativeName
			+ "? You can always change your language in Settings.",
			"Change",
			tr::lng_cancel(tr::now),
			confirm,
			cancel),
		Ui::LayerOption::KeepOther);
	return true;
}

void CloudManager::applyLangPackData(
		Pack pack,
		const MTPDlangPackDifference &data) {
	if (_langpack.version(pack) < data.vfrom_version().v) {
		requestLangPackDifference(pack);
	} else if (!data.vstrings().v.isEmpty()) {
		_langpack.applyDifference(pack, data);
		Local::writeLangPack();
	} else if (_restartAfterSwitch) {
		Local::writeLangPack();
	} else {
		LOG(("Lang Info: Up to date."));
	}
}

bool CloudManager::canApplyWithoutRestart(const QString &id) const {
	if (id == qstr("#TEST_X") || id == qstr("#TEST_0")) {
		return true;
	}
	return Core::App().canApplyLangPackWithoutRestart();
}

void CloudManager::resetToDefault() {
	performSwitch(DefaultLanguage());
}

void CloudManager::switchToLanguage(const QString &id) {
	requestLanguageAndSwitch(id, false);
}

void CloudManager::switchWithWarning(const QString &id) {
	requestLanguageAndSwitch(id, true);
}

void CloudManager::requestLanguageAndSwitch(
		const QString &id,
		bool warning) {
	Expects(!id.isEmpty());

	if (LanguageIdOrDefault(_langpack.id()) == id) {
		Ui::show(Box<InformBox>(tr::lng_language_already(tr::now)));
		return;
	} else if (id == qstr("#custom")) {
		performSwitchToCustom();
		return;
	}

	_switchingToLanguageId = id;
	_switchingToLanguageWarning = warning;
	sendSwitchingToLanguageRequest();
}

void CloudManager::sendSwitchingToLanguageRequest() {
	if (!_api) {
		_switchingToLanguageId = -1;
		return;
	}
	_api->request(_switchingToLanguageRequest).cancel();
	_switchingToLanguageRequest = _api->request(MTPlangpack_GetLanguage(
		MTP_string(Lang::CloudLangPackName()),
		MTP_string(_switchingToLanguageId)
	)).done([=](const MTPLangPackLanguage &result) {
		_switchingToLanguageRequest = 0;
		const auto language = Lang::ParseLanguage(result);
		const auto finalize = [=] {
			if (canApplyWithoutRestart(language.id)) {
				performSwitchAndAddToRecent(language);
			} else {
				performSwitchAndRestart(language);
			}
		};
		if (!_switchingToLanguageWarning) {
			finalize();
			return;
		}
		result.match([=](const MTPDlangPackLanguage &data) {
			if (data.vstrings_count().v > 0) {
				Ui::show(Box<ConfirmSwitchBox>(data, finalize));
			} else {
				Ui::show(Box<NotReadyBox>(data));
			}
		});
	}).fail([=](const MTP::Error &error) {
		_switchingToLanguageRequest = 0;
		if (error.type() == "LANG_CODE_NOT_SUPPORTED") {
			Ui::show(Box<InformBox>(tr::lng_language_not_found(tr::now)));
		}
	}).send();
}

void CloudManager::switchToLanguage(const Language &data) {
	if (_langpack.id() == data.id && data.id != qstr("#custom")) {
		return;
	} else if (!_api) {
		return;
	}

	_api->request(base::take(_getKeysForSwitchRequestId)).cancel();
	if (data.id == qstr("#custom")) {
		performSwitchToCustom();
	} else if (canApplyWithoutRestart(data.id)) {
		performSwitchAndAddToRecent(data);
	} else {
		QVector<MTPstring> keys;
		keys.reserve(3);
		keys.push_back(MTP_string("lng_sure_save_language"));
		_getKeysForSwitchRequestId = _api->request(MTPlangpack_GetStrings(
			MTP_string(Lang::CloudLangPackName()),
			MTP_string(data.id),
			MTP_vector<MTPstring>(std::move(keys))
		)).done([=](const MTPVector<MTPLangPackString> &result) {
			_getKeysForSwitchRequestId = 0;
			const auto values = Instance::ParseStrings(result);
			const auto getValue = [&](ushort key) {
				auto it = values.find(key);
				return (it == values.cend())
					? GetOriginalValue(key)
					: it->second;
			};
			const auto text = tr::lng_sure_save_language(tr::now)
				+ "\n\n"
				+ getValue(tr::lng_sure_save_language.base);
			Ui::show(
				Box<ConfirmBox>(
					text,
					tr::lng_box_ok(tr::now),
					tr::lng_cancel(tr::now),
					[=] { performSwitchAndRestart(data); }),
				Ui::LayerOption::KeepOther);
		}).fail([=](const MTP::Error &error) {
			_getKeysForSwitchRequestId = 0;
		}).send();
	}
}

void CloudManager::performSwitchToCustom() {
	auto filter = qsl("Language files (*.strings)");
	auto title = qsl("Choose language .strings file");
	FileDialog::GetOpenPath(Core::App().getFileDialogParent(), title, filter, [=, weak = base::make_weak(this)](const FileDialog::OpenResult &result) {
		if (!weak || result.paths.isEmpty()) {
			return;
		}

		const auto filePath = result.paths.front();
		auto loader = Lang::FileParser(
			filePath,
			{ tr::lng_sure_save_language.base });
		if (loader.errors().isEmpty()) {
			if (_api) {
				_api->request(
					base::take(_switchingToLanguageRequest)
				).cancel();
			}
			if (canApplyWithoutRestart(qsl("#custom"))) {
				_langpack.switchToCustomFile(filePath);
			} else {
				const auto values = loader.found();
				const auto getValue = [&](ushort key) {
					const auto it = values.find(key);
					return (it == values.cend())
						? GetOriginalValue(key)
						: it.value();
				};
				const auto text = tr::lng_sure_save_language(tr::now)
					+ "\n\n"
					+ getValue(tr::lng_sure_save_language.base);
				const auto change = [=] {
					_langpack.switchToCustomFile(filePath);
					App::restart();
				};
				Ui::show(
					Box<ConfirmBox>(
						text,
						tr::lng_box_ok(tr::now),
						tr::lng_cancel(tr::now),
						change),
					Ui::LayerOption::KeepOther);
			}
		} else {
			Ui::show(
				Box<InformBox>("Custom lang failed :(\n\nError: " + loader.errors()),
				Ui::LayerOption::KeepOther);
		}
	});
}

void CloudManager::switchToTestLanguage() {
	const auto testLanguageId = (_langpack.id() == qstr("#TEST_X"))
		? qsl("#TEST_0")
		: qsl("#TEST_X");
	performSwitch({ testLanguageId });
}

void CloudManager::performSwitch(const Language &data) {
	_restartAfterSwitch = false;
	switchLangPackId(data);
	requestLangPackDifference(Pack::Current);
	requestLangPackDifference(Pack::Base);
}

void CloudManager::performSwitchAndAddToRecent(const Language &data) {
	Local::pushRecentLanguage(data);
	performSwitch(data);
}

void CloudManager::performSwitchAndRestart(const Language &data) {
	performSwitchAndAddToRecent(data);
	restartAfterSwitch();
}

void CloudManager::restartAfterSwitch() {
	if (_langPackRequestId || _langPackBaseRequestId) {
		_restartAfterSwitch = true;
	} else {
		App::restart();
	}
}

void CloudManager::switchLangPackId(const Language &data) {
	const auto currentId = _langpack.id();
	const auto currentBaseId = _langpack.baseId();
	const auto notChanged = (currentId == data.id
		&& currentBaseId == data.baseId)
		|| (currentId.isEmpty()
			&& currentBaseId.isEmpty()
			&& data.id == DefaultLanguageId());
	if (!notChanged) {
		changeIdAndReInitConnection(data);
	}
}

void CloudManager::changeIdAndReInitConnection(const Language &data) {
	_langpack.switchToId(data);
	if (_api) {
		const auto mtproto = &_api->instance();
		mtproto->reInitConnection(mtproto->mainDcId());
	}
}

void CloudManager::resendRequests() {
	if (packRequestId(Pack::Base)) {
		requestLangPackDifference(Pack::Base);
	}
	if (packRequestId(Pack::Current)) {
		requestLangPackDifference(Pack::Current);
	}
	if (_languagesRequestId) {
		requestLanguageList();
	}
	if (_switchingToLanguageRequest) {
		sendSwitchingToLanguageRequest();
	}
}

CloudManager &CurrentCloudManager() {
	auto result = Core::App().langCloudManager();
	Assert(result != nullptr);
	return *result;
}

} // namespace Lang
