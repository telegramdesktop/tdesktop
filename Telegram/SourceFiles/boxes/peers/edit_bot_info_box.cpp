/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_bot_info_box.h"

#include "api/api_bot_info.h"
#include "apiwrap.h"
#include "boxes/compose_ai_box.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"

namespace {

using Field = Data::BotInfoField;
using Job = Api::BotInfoLocalization::Job;
using JobState = Api::BotInfoLocalization::State;

constexpr auto kActionsSeparator = QChar(0xB7);

[[nodiscard]] rpl::producer<QString> FieldTitle(Field field) {
	switch (field) {
	case Field::Name: return tr::lng_bot_info_name();
	case Field::About: return tr::lng_bot_info_about();
	case Field::Description: return tr::lng_bot_info_intro();
	}
	Unexpected("Field in FieldTitle.");
}

[[nodiscard]] rpl::producer<QString> FieldPlaceholder(Field field) {
	switch (field) {
	case Field::Name: return tr::lng_bot_info_name_placeholder();
	case Field::About: return tr::lng_bot_info_about_placeholder();
	case Field::Description: return tr::lng_bot_info_intro_placeholder();
	}
	Unexpected("Field in FieldPlaceholder.");
}

[[nodiscard]] not_null<Ui::InputField*> AddField(
		not_null<Ui::VerticalLayout*> container,
		Field field,
		const QString &value,
		const QString &inherited) {
	Ui::AddSubsectionTitle(
		container,
		FieldTitle(field),
		st::botInfoTitlePadding);
	const auto result = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::botInfoField,
			((field == Field::Name)
				? Ui::InputField::Mode::SingleLine
				: Ui::InputField::Mode::MultiLine),
			(inherited.isEmpty()
				? FieldPlaceholder(field)
				: rpl::single(inherited)),
			value),
		st::botInfoFieldPadding);
	Ui::AddLengthLimitLabel(result, Data::BotInfoFieldLimit(field), {
		.customCharactersCount = [=] {
			return Data::BotInfoTextLength(result->getLastText());
		},
	});
	return result;
}

void AddFieldActions(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot,
		const QString &code,
		not_null<Ui::InputField*> input,
		const QString &inherited) {
	const auto localization
		= bot->session().api().botInfoLocalizations().get(bot);
	const auto translate = [=] {
		const auto done = crl::guard(input, [=](QString result) {
			input->setText(result);
			input->setFocus();
		});
		const auto fail = crl::guard(box, [=](QString error) {
			box->showToast((error.isEmpty() || error.startsWith(u"FLOOD"_q))
				? tr::lng_flood_error(tr::now)
				: error);
		});
		localization->translateOne(inherited, code, done, fail);
	};
	const auto rewrite = [=] {
		const auto current = input->getLastText().trimmed();
		const auto source = current.isEmpty() ? inherited : current;
		if (source.isEmpty()) {
			input->showError();
			return;
		}
		HistoryView::Controls::ShowComposeAiBox(box->uiShow(), {
			.session = &bot->session(),
			.text = tr::marked(source),
			.apply = crl::guard(input, [=](TextWithEntities result) {
				input->setText(result.text);
			}),
			.allowPrompt = true,
		});
	};
	const auto reset = [=] {
		input->setText(QString());
		input->setFocus();
	};
	const auto translatable = !inherited.isEmpty();
	const auto resettable = !code.isEmpty();
	auto text = rpl::combine(
		tr::lng_bot_info_action_translate(),
		tr::lng_bot_info_action_ai(),
		tr::lng_bot_info_action_reset()
	) | rpl::map([=](
			const QString &translateText,
			const QString &rewriteText,
			const QString &resetText) {
		const auto separator = u"  "_q + kActionsSeparator + u"  "_q;
		auto result = tr::marked();
		if (translatable) {
			result.append(tr::link(translateText, 1)).append(separator);
		}
		result.append(tr::link(rewriteText, 2));
		if (resettable) {
			result.append(separator).append(tr::link(resetText, 3));
		}
		return result;
	});
	const auto label = box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boxDividerLabel),
		st::botInfoActionsPadding);
	label->setLink(1, std::make_shared<LambdaClickHandler>(translate));
	label->setLink(2, std::make_shared<LambdaClickHandler>(rewrite));
	label->setLink(3, std::make_shared<LambdaClickHandler>(reset));
}

} // namespace

QString BotInfoLanguageName(const QString &langCode) {
	const auto id = LanguageId::FromName(langCode);
	return (id.twoLetterCode() == langCode)
		? Ui::LanguageName(id)
		: langCode.toUpper();
}

void EditBotInfoBox(
		not_null<Ui::GenericBox*> box,
		EditBotInfoBoxArgs &&args) {
	struct State {
		std::array<Ui::InputField*, Data::kBotInfoFields.size()> fields = {};
		bool applying = false;
	};
	const auto bot = args.bot;
	const auto code = args.langCode;
	const auto onlyIntro = args.onlyIntro;
	const auto applyOnSave = args.applyOnSave;
	const auto localization
		= bot->session().api().botInfoLocalizations().get(bot);
	const auto draft = &localization->draft();
	const auto state = box->lifetime().make_state<State>();
	const auto container = box->verticalLayout();

	box->setTitle(onlyIntro
		? tr::lng_manage_peer_bot_edit_intro()
		: code.isEmpty()
		? tr::lng_bot_info_title()
		: rpl::single(BotInfoLanguageName(code)));
	box->setWidth(st::boxWideWidth);

	const auto loading = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_contacts_loading(),
				st::boxDividerLabel),
			st::botInfoLoadingPadding));

	const auto shown = [=](Field field) {
		return !onlyIntro || (field == Field::Description);
	};
	const auto collect = [=] {
		for (const auto field : Data::kBotInfoFields) {
			const auto input = state->fields[int(field)];
			if (!input) {
				continue;
			}
			const auto text = Data::NormalizeBotInfoText(
				field,
				input->getLastText());
			const auto limit = Data::BotInfoFieldLimit(field);
			const auto emptyName = code.isEmpty()
				&& (field == Field::Name)
				&& text.isEmpty();
			if (emptyName || (Data::BotInfoTextLength(text) > limit)) {
				input->showError();
				return false;
			}
		}
		for (const auto field : Data::kBotInfoFields) {
			if (const auto input = state->fields[int(field)]) {
				draft->stage(code, field, input->getLastText());
			}
		}
		return true;
	};
	const auto applied = [=] {
		state->applying = false;
		const auto language = draft->find(code);
		if (!localization->state().stopped.isEmpty()) {
			box->showToast(tr::lng_flood_error(tr::now));
		} else if (language && !language->error.isEmpty()) {
			box->showToast(language->error);
		} else {
			box->showToast(tr::lng_bot_info_saved(tr::now));
			box->closeBox();
		}
	};
	const auto save = [=] {
		if (state->applying || !state->fields[int(Field::Description)]) {
			return;
		} else if (!collect()) {
			return;
		}
		const auto language = draft->find(code);
		if (!applyOnSave || !language || language->staged.empty()) {
			box->closeBox();
			return;
		}
		state->applying = true;
		localization->applyOne(code);
		localization->stateValue(
		) | rpl::filter([](const JobState &state) {
			return (state.job == Job::None);
		}) | rpl::take(1) | rpl::on_next(applied, box->lifetime());
	};
	const auto fill = [=] {
		loading->hide(anim::type::instant);
		const auto language = draft->find(code);
		const auto &fallback = draft->fallback();
		for (const auto field : Data::kBotInfoFields) {
			if (!shown(field)) {
				continue;
			}
			const auto input = AddField(
				container,
				field,
				language ? language->value(field) : QString(),
				code.isEmpty() ? QString() : fallback.value(field));
			state->fields[int(field)] = input;
			AddFieldActions(
				box,
				bot,
				code,
				input,
				code.isEmpty() ? QString() : fallback.value(field));
			if (field == Field::Description) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						tr::lng_bot_info_intro_caption(),
						st::boxDividerLabel),
					st::botInfoCaptionPadding);
			} else if (field == Field::About) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						(code.isEmpty()
							? tr::lng_bot_info_about_caption()
							: tr::lng_bot_info_language_caption()),
						st::boxDividerLabel),
					st::botInfoCaptionPadding);
			}
		}
		const auto name = state->fields[int(Field::Name)];
		const auto about = state->fields[int(Field::About)];
		if (name && about) {
			name->submits() | rpl::on_next([=] {
				about->setFocus();
			}, name->lifetime());
		}
		const auto first = name
			? name
			: state->fields[int(Field::Description)];
		box->setFocusCallback([=] { first->setFocusFast(); });
		first->setFocusFast();
	};
	const auto ready = [=] {
		return localization->known(QString())
			&& (code.isEmpty()
				|| localization->known(code)
				|| draft->find(code));
	};

	localization->stateValue(
	) | rpl::on_next([=](const JobState &now) {
		if (state->fields[int(Field::Description)]) {
			return;
		} else if (ready()) {
			fill();
		} else if (now.job == Job::None) {
			crl::on_main(box, [=] {
				if (!ready() && (localization->state().job == Job::None)) {
					localization->load(code.isEmpty()
						? std::vector<QString>{ QString() }
						: std::vector<QString>{ QString(), code });
				}
			});
		}
	}, box->lifetime());

	box->addButton(
		applyOnSave ? tr::lng_settings_save() : tr::lng_about_done(),
		save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}
