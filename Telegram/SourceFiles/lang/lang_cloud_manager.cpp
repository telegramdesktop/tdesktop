/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_cloud_manager.h"

#include "lang/lang_instance.h"
#include "mtproto/mtp_instance.h"
#include "storage/localstorage.h"
#include "messenger.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "lang/lang_file_parser.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "styles/style_boxes.h"

namespace Lang {
namespace {

class ConfirmSwitchBox : public BoxContent {
public:
	ConfirmSwitchBox(
		QWidget*,
		const MTPDlangPackLanguage &data,
		const QString &editLink,
		Fn<void()> apply);

protected:
	void prepare() override;

private:
	QString _name;
	int _percent = 0;
	QString _editLink;
	Fn<void()> _apply;

};

class NotReadyBox : public BoxContent {
public:
	NotReadyBox(
		QWidget*,
		const MTPDlangPackLanguage &data,
		const QString &editLink);

protected:
	void prepare() override;

private:
	QString _name;
	QString _editLink;

};

ConfirmSwitchBox::ConfirmSwitchBox(
	QWidget*,
	const MTPDlangPackLanguage &data,
	const QString &editLink,
	Fn<void()> apply)
: _name(qs(data.vnative_name))
, _percent(data.vtranslated_count.v * 100 / data.vstrings_count.v)
, _editLink(editLink)
, _apply(std::move(apply)) {
}

void ConfirmSwitchBox::prepare() {
	setTitle(langFactory(lng_language_switch_title));

	auto link = TextWithEntities{ lang(lng_language_switch_link) };
	link.entities.push_back(EntityInText(
		EntityInTextCustomUrl,
		0,
		link.text.size(),
		QString("internal:go_to_translations")));
	auto name = TextWithEntities{ _name };
	name.entities.push_back(EntityInText(
		EntityInTextBold,
		0,
		name.text.size()));
	auto percent = TextWithEntities{ QString::number(_percent) };
	percent.entities.push_back(EntityInText(
		EntityInTextBold,
		0,
		percent.text.size()));
	const auto text = lng_language_switch_about__generic(
		lt_lang_name,
		name,
		lt_percent,
		percent,
		lt_link,
		link);
	auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			rpl::single(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setClickHandlerFilter([=](auto&&...) {
		UrlClickHandler::Open(_editLink);
		return false;
	});

	addButton(langFactory(lng_language_switch_apply), [=] {
		const auto apply = _apply;
		closeBox();
		apply();
	});
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	content->resizeToWidth(st::boxWideWidth);
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, lifetime());
}

NotReadyBox::NotReadyBox(
	QWidget*,
	const MTPDlangPackLanguage &data,
	const QString &editLink)
: _name(qs(data.vnative_name))
, _editLink(editLink) {
}

void NotReadyBox::prepare() {
	setTitle(langFactory(lng_language_not_ready_title));

	auto link = TextWithEntities{ lang(lng_language_not_ready_link) };
	link.entities.push_back(EntityInText(
		EntityInTextCustomUrl,
		0,
		link.text.size(),
		QString("internal:go_to_translations")));
	auto name = TextWithEntities{ _name };
	const auto text = lng_language_not_ready_about__generic(
		lt_lang_name,
		name,
		lt_link,
		link);
	auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			rpl::single(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setClickHandlerFilter([=](auto&&...) {
		UrlClickHandler::Open(_editLink);
		return false;
	});

	addButton(langFactory(lng_box_ok), [=] { closeBox(); });

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
			qs(data.vlang_code),
			qs(data.vplural_code),
			(data.has_base_lang_code()
				? qs(data.vbase_lang_code)
				: QString()),
			qs(data.vname),
			qs(data.vnative_name)
		};
	});
}

CloudManager::CloudManager(
	Instance &langpack,
	not_null<MTP::Instance*> mtproto)
: MTP::Sender()
, _langpack(langpack) {
}

Pack CloudManager::packTypeFromId(const QString &id) const {
	if (id == LanguageIdOrDefault(_langpack.id())) {
		return Pack::Current;
	} else if (id == _langpack.baseId()) {
		return Pack::Base;
	}
	return Pack::None;
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
	const auto base = (pack == Pack::Base);
	request(base::take(packRequestId(pack))).cancel();
	if (_langpack.isCustom()) {
		return;
	}

	const auto version = _langpack.version(pack);
	const auto code = _langpack.cloudLangCode(pack);
	if (code.isEmpty()) {
		return;
	}
	if (version > 0) {
		packRequestId(pack) = request(MTPlangpack_GetDifference(
			MTP_string(code),
			MTP_int(version)
		)).done([=](const MTPLangPackDifference &result) {
			packRequestId(pack) = 0;
			applyLangPackDifference(result);
		}).fail([=](const RPCError &error) {
			packRequestId(pack) = 0;
		}).send();
	} else {
		packRequestId(pack) = request(MTPlangpack_GetLangPack(
			MTP_string(CloudLangPackName()),
			MTP_string(code)
		)).done([=](const MTPLangPackDifference &result) {
			packRequestId(pack) = 0;
			applyLangPackDifference(result);
		}).fail([=](const RPCError &error) {
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
	const auto langpackId = qs(langpack.vlang_code);
	const auto pack = packTypeFromId(langpackId);
	if (pack != Pack::None) {
		applyLangPackData(pack, langpack);
		if (_restartAfterSwitch) {
			restartAfterSwitch();
		}
	} else {
		LOG(("Lang Warning: "
			"Ignoring update for '%1' because our language is '%2'"
			).arg(langpackId
			).arg(_langpack.id()));
	}
}

void CloudManager::requestLanguageList() {
	_languagesRequestId = request(MTPlangpack_GetLanguages(
		MTP_string(CloudLangPackName())
	)).done([=](const MTPVector<MTPLangPackLanguage> &result) {
		auto languages = Languages();
		for (const auto &language : result.v) {
			languages.push_back(ParseLanguage(language));
		}
		if (_languages != languages) {
			_languages = languages;
			_languagesChanged.notify();
		}
		_languagesRequestId = 0;
	}).fail([=](const RPCError &error) {
		_languagesRequestId = 0;
	}).send();
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
	for (const auto &language : _languages) {
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
	}), LayerOption::KeepOther);
	return true;
}

void CloudManager::applyLangPackData(
		Pack pack,
		const MTPDlangPackDifference &data) {
	if (_langpack.version(pack) < data.vfrom_version.v) {
		requestLangPackDifference(pack);
	} else if (!data.vstrings.v.isEmpty()) {
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

	// We don't support instant language switch if the auth session exists :(
	return !AuthSession::Exists();
}

void CloudManager::resetToDefault() {
	performSwitch(DefaultLanguageId());
}

void CloudManager::switchWithWarning(const QString &id) {
	Expects(!id.isEmpty());

	if (LanguageIdOrDefault(_langpack.id()) == id) {
		Ui::show(Box<InformBox>(lang(lng_language_already)));
		return;
	}

	request(_switchingToLanguageRequest).cancel();
	_switchingToLanguageRequest = request(MTPlangpack_GetLanguage(
		MTP_string(Lang::CloudLangPackName()),
		MTP_string(id)
	)).done([=](const MTPLangPackLanguage &result) {
		_switchingToLanguageRequest = 0;
		result.match([=](const MTPDlangPackLanguage &data) {
			const auto link = "https://translations.telegram.org/"
				+ id
				+ '/';
			if (data.vtranslated_count.v > 0) {
				const auto pluralId = qs(data.vplural_code);
				const auto baseId = qs(data.vbase_lang_code);
				const auto perform = [=] {
					Local::pushRecentLanguage(ParseLanguage(result));
					performSwitchAndRestart(id, pluralId, baseId);
				};
				Ui::show(Box<ConfirmSwitchBox>(data, link, perform));
			} else {
				Ui::show(Box<NotReadyBox>(data, link));
			}
		});
	}).fail([=](const RPCError &error) {
		_switchingToLanguageRequest = 0;
		if (error.type() == "LANG_CODE_NOT_SUPPORTED") {
			Ui::show(Box<InformBox>(lang(lng_language_not_found)));
		}
	}).send();
}

void CloudManager::switchToLanguage(
		const QString &id,
		const QString &pluralId,
		const QString &baseId) {
	const auto requested = LanguageIdOrDefault(id);
	if (_langpack.id() == requested && requested != qstr("#custom")) {
		return;
	}

	request(_switchingToLanguageRequest).cancel();
	if (requested == qstr("#custom")) {
		performSwitchToCustom();
	} else if (canApplyWithoutRestart(requested)) {
		performSwitch(requested, pluralId, baseId);
	} else {
		QVector<MTPstring> keys;
		keys.reserve(3);
		keys.push_back(MTP_string("lng_sure_save_language"));
		_switchingToLanguageRequest = request(MTPlangpack_GetStrings(
			MTP_string(Lang::CloudLangPackName()),
			MTP_string(requested),
			MTP_vector<MTPstring>(std::move(keys))
		)).done([=](const MTPVector<MTPLangPackString> &result) {
			_switchingToLanguageRequest = 0;
			const auto values = Instance::ParseStrings(result);
			const auto getValue = [&](LangKey key) {
				auto it = values.find(key);
				return (it == values.cend())
					? GetOriginalValue(key)
					: it->second;
			};
			const auto text = lang(lng_sure_save_language)
				+ "\n\n"
				+ getValue(lng_sure_save_language);
			const auto perform = [=] {
				performSwitchAndRestart(requested, pluralId, baseId);
			};
			Ui::show(
				Box<ConfirmBox>(
					text,
					lang(lng_box_ok),
					lang(lng_cancel),
					perform),
				LayerOption::KeepOther);
		}).fail([=](const RPCError &error) {
			_switchingToLanguageRequest = 0;
		}).send();
	}
}

void CloudManager::performSwitchToCustom() {
	auto filter = qsl("Language files (*.strings)");
	auto title = qsl("Choose language .strings file");
	FileDialog::GetOpenPath(Messenger::Instance().getFileDialogParent(), title, filter, [weak = base::make_weak(this)](const FileDialog::OpenResult &result) {
		if (!weak || result.paths.isEmpty()) {
			return;
		}

		auto filePath = result.paths.front();
		Lang::FileParser loader(filePath, { lng_sure_save_language });
		if (loader.errors().isEmpty()) {
			weak->request(weak->_switchingToLanguageRequest).cancel();
			if (weak->canApplyWithoutRestart(qsl("#custom"))) {
				weak->_langpack.switchToCustomFile(filePath);
			} else {
				const auto values = loader.found();
				const auto getValue = [&](LangKey key) {
					const auto it = values.find(key);
					return (it == values.cend())
						? GetOriginalValue(key)
						: it.value();
				};
				const auto text = lang(lng_sure_save_language)
					+ "\n\n"
					 + getValue(lng_sure_save_language);
				const auto change = [=] {
					weak->_langpack.switchToCustomFile(filePath);
					App::restart();
				};
				Ui::show(
					Box<ConfirmBox>(
						text,
						lang(lng_box_ok),
						lang(lng_cancel),
						change),
					LayerOption::KeepOther);
			}
		} else {
			Ui::show(
				Box<InformBox>("Custom lang failed :(\n\nError: " + loader.errors()),
				LayerOption::KeepOther);
		}
	});
}

void CloudManager::switchToTestLanguage() {
	const auto testLanguageId = (_langpack.id() == qstr("#TEST_X"))
		? qsl("#TEST_0")
		: qsl("#TEST_X");
	performSwitch(testLanguageId);
}

void CloudManager::performSwitch(
		const QString &id,
		const QString &pluralId,
		const QString &baseId) {
	_restartAfterSwitch = false;
	switchLangPackId(id, pluralId, baseId);
	requestLangPackDifference(Pack::Current);
	requestLangPackDifference(Pack::Base);
}

void CloudManager::performSwitchAndRestart(
		const QString &id,
		const QString &pluralId,
		const QString &baseId) {
	performSwitch(id, pluralId, baseId);
	restartAfterSwitch();
}

void CloudManager::restartAfterSwitch() {
	if (_langPackRequestId || _langPackBaseRequestId) {
		_restartAfterSwitch = true;
	} else {
		App::restart();
	}
}

void CloudManager::switchLangPackId(
		const QString &id,
		const QString &pluralId,
		const QString &baseId) {
	const auto currentId = _langpack.id();
	const auto currentBaseId = _langpack.baseId();
	const auto notChanged = (currentId == id && currentBaseId == baseId)
		|| (currentId.isEmpty()
			&& currentBaseId.isEmpty()
			&& id == DefaultLanguageId());
	if (!notChanged) {
		changeIdAndReInitConnection(id, pluralId, baseId);
	}
}

void CloudManager::changeIdAndReInitConnection(
		const QString &id,
		const QString &pluralId,
		const QString &baseId) {
	_langpack.switchToId(id, pluralId, baseId);

	auto mtproto = requestMTP();
	mtproto->reInitConnection(mtproto->mainDcId());
}

CloudManager &CurrentCloudManager() {
	auto result = Messenger::Instance().langCloudManager();
	Assert(result != nullptr);
	return *result;
}

} // namespace Lang
