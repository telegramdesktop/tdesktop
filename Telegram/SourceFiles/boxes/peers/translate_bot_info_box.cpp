/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/translate_bot_info_box.h"

#include "api/api_bot_info.h"
#include "apiwrap.h"
#include "boxes/peers/bot_info_field.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace {

using Field = Data::BotInfoField;
using Job = Api::BotInfoLocalization::Job;

[[nodiscard]] std::optional<LanguageId> LanguageByCode(const QString &code) {
	const auto id = LanguageId::FromName(code);
	return (id.twoLetterCode() == code) ? id : std::optional<LanguageId>();
}

} // namespace

void TranslateBotInfoBox(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot) {
	struct State {
		std::array<Ui::Checkbox*, Data::kBotInfoFields.size()> fields = {};
		rpl::variable<std::vector<LanguageId>> languages;
		std::vector<QString> otherCodes;
	};
	const auto localization
		= bot->session().api().botInfoLocalizations().get(bot);
	const auto draft = &localization->draft();
	const auto state = box->lifetime().make_state<State>();
	const auto container = box->verticalLayout();

	box->setTitle(tr::lng_bot_languages_translate_title());
	box->setWidth(st::boxWideWidth);

	for (const auto field : Data::kBotInfoFields) {
		const auto available = !draft->fallback().value(field).isEmpty();
		const auto checkbox = container->add(
			object_ptr<Ui::Checkbox>(
				container,
				BotInfoFieldTitle(field),
				available && (field != Field::Name),
				st::defaultBoxCheckbox),
			st::botInfoCheckboxPadding);
		checkbox->setDisabled(!available);
		state->fields[int(field)] = checkbox;
	}

	auto listed = std::vector<LanguageId>();
	for (const auto &language : draft->languages()) {
		if (language.fallback()) {
			continue;
		} else if (const auto id = LanguageByCode(language.code)) {
			listed.push_back(*id);
		} else {
			state->otherCodes.push_back(language.code);
		}
	}
	state->languages = std::move(listed);

	const auto choose = [=] {
		box->uiShow()->showBox(Box(
			Ui::ChooseLanguageBox,
			tr::lng_bot_languages_translate_languages(),
			[=](std::vector<LanguageId> ids) { state->languages = ids; },
			state->languages.current(),
			true,
			nullptr));
	};
	const auto otherCount = int(state->otherCodes.size());
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_bot_languages_translate_languages(),
		state->languages.value(
		) | rpl::map([=](const std::vector<LanguageId> &list) {
			return QString::number(int(list.size()) + otherCount);
		}),
		choose,
		st::manageGroupNoIconButton,
		{}));
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_bot_languages_translate_about(),
			st::boxDividerLabel),
		st::botLanguagesAboutPadding);

	box->addButton(tr::lng_bot_languages_translate_button(), [=] {
		auto request = Api::BotInfoLocalization::TranslateRequest{
			.codes = state->otherCodes,
		};
		for (const auto id : state->languages.current()) {
			request.codes.push_back(id.twoLetterCode());
		}
		for (const auto field : Data::kBotInfoFields) {
			if (state->fields[int(field)]->checked()) {
				request.fields.push_back(field);
			}
		}
		if (request.codes.empty() || request.fields.empty()) {
			box->showToast(tr::lng_bot_languages_translate_empty(tr::now));
		} else if (localization->state().job != Job::None) {
			box->showToast(tr::lng_bot_languages_busy(tr::now));
		} else {
			for (const auto &code : request.codes) {
				draft->add(code);
			}
			localization->translate(std::move(request));
			box->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}
