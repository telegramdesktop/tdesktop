/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_bot_languages_box.h"

#include "api/api_bot_info.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "boxes/peers/edit_bot_info_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"

#include "styles/style_edit_peer_members.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace {

using Job = Api::BotInfoLocalization::Job;
using JobState = Api::BotInfoLocalization::State;

constexpr auto kCountdownStep = crl::time(500);

struct Row {
	QString code;
	rpl::variable<QString> status;
};

[[nodiscard]] std::vector<QString> KnownLanguageCodes() {
	auto result = std::vector<QString>();
	for (const auto id : Ui::TranslationLanguages()) {
		const auto code = id.twoLetterCode();
		if (Data::ValidBotInfoLangCode(code)
			&& !ranges::contains(result, code)) {
			result.push_back(code);
		}
	}
	return result;
}

[[nodiscard]] rpl::producer<QString> RowTitle(
		const Data::BotInfoLanguage &language) {
	return language.fallback()
		? tr::lng_bot_languages_default()
		: rpl::single(BotInfoLanguageName(language.code));
}

[[nodiscard]] QString RowStatus(const Data::BotInfoLanguage &language) {
	if (!language.error.isEmpty()) {
		return language.error;
	}
	for (const auto field : Data::kBotInfoFields) {
		switch (language.problem(field)) {
		case Data::BotInfoProblem::TooLong:
			return tr::lng_bot_languages_too_long(tr::now);
		case Data::BotInfoProblem::EmptyName:
			return tr::lng_bot_languages_no_name(tr::now);
		case Data::BotInfoProblem::None: break;
		}
	}
	if (!language.staged.empty()) {
		return tr::lng_bot_languages_edited(tr::now);
	} else if (language.fallback()) {
		return QString();
	}
	return u"%1/%2"_q
		.arg(language.filledCount())
		.arg(Data::kBotInfoFields.size());
}

[[nodiscard]] QString JobStatus(const JobState &state) {
	if (!state.stopped.isEmpty()) {
		return tr::lng_bot_languages_stopped(tr::now);
	} else if (state.pausedTill) {
		const auto left = std::max(
			state.pausedTill - crl::now(),
			crl::time(0));
		return tr::lng_bot_languages_paused(
			tr::now,
			lt_time,
			Ui::FormatDurationText((left + 999) / 1000));
	}
	const auto done = QString::number(state.done);
	const auto total = QString::number(state.total);
	switch (state.job) {
	case Job::Load:
		return tr::lng_bot_languages_loading(
			tr::now,
			lt_done,
			done,
			lt_total,
			total);
	case Job::Apply:
		return tr::lng_bot_languages_applying(
			tr::now,
			lt_done,
			done,
			lt_total,
			total);
	}
	return QString();
}

[[nodiscard]] int ApplicableChanges(const Data::BotInfoDraft &draft) {
	auto result = 0;
	for (const auto &language : draft.languages()) {
		if (!language.hasProblem()) {
			result += language.staged.count();
		}
	}
	return result;
}

[[nodiscard]] auto SortedLanguages(const Data::BotInfoDraft &draft) {
	auto result = std::vector<not_null<const Data::BotInfoLanguage*>>();
	for (const auto &language : draft.languages()) {
		result.push_back(&language);
	}
	ranges::sort(result, [](const auto &a, const auto &b) {
		if (a->fallback() || b->fallback()) {
			return a->fallback() && !b->fallback();
		}
		const auto first = BotInfoLanguageName(a->code);
		const auto second = BotInfoLanguageName(b->code);
		return first.compare(second, Qt::CaseInsensitive) < 0;
	});
	return result;
}

} // namespace

rpl::producer<QString> BotLanguagesStatusValue(not_null<UserData*> bot) {
	const auto draft = &bot->session().api().botInfoLocalizations().get(
		bot)->draft();
	return rpl::single(
		rpl::empty
	) | rpl::then(draft->updates()) | rpl::map([=] {
		const auto changes = draft->changesCount();
		const auto count = int(draft->languages().size()) - 1;
		return changes
			? tr::lng_manage_peer_bot_languages_unsaved(
				tr::now,
				lt_count,
				changes)
			: count
			? QString::number(count)
			: QString();
	});
}

void TrackBotInfoField(
		not_null<Ui::InputField*> field,
		rpl::producer<QString> value) {
	const auto last = field->lifetime().make_state<QString>(
		field->getLastText());
	std::move(value) | rpl::on_next([=](const QString &now) {
		if (field->getLastText() == *last) {
			field->setText(now);
		}
		*last = now;
	}, field->lifetime());
}

void EditBotLanguagesBox(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot) {
	struct State {
		std::vector<std::unique_ptr<Row>> rows;
		rpl::variable<QString> status;
		rpl::variable<int> changes;
		base::Timer countdown;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto localization
		= bot->session().api().botInfoLocalizations().get(bot);
	const auto draft = &localization->draft();
	const auto state = box->lifetime().make_state<State>();
	const auto container = box->verticalLayout();

	box->setTitle(tr::lng_bot_languages_title());
	box->setWidth(st::boxWideWidth);

	const auto status = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				state->status.value(),
				st::boxDividerLabel),
			st::botInfoCaptionPadding));
	status->toggleOn(state->status.value(
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));

	const auto list = container->add(
		object_ptr<Ui::VerticalLayout>(container));

	const auto edit = [=](const QString &code) {
		box->uiShow()->showBox(Box(EditBotInfoBox, EditBotInfoBoxArgs{
			.bot = bot,
			.langCode = code,
		}));
	};
	const auto refresh = [=] {
		const auto languages = SortedLanguages(*draft);
		const auto same = ranges::equal(
			languages,
			state->rows,
			ranges::equal_to(),
			&Data::BotInfoLanguage::code,
			&Row::code);
		if (!same) {
			list->clear();
			state->rows.clear();
			for (const auto &language : languages) {
				const auto code = language->code;
				auto row = std::make_unique<Row>();
				row->code = code;
				list->add(EditPeerInfoBox::CreateButton(
					list,
					RowTitle(*language),
					row->status.value(),
					[=] { edit(code); },
					st::manageGroupNoIconButton,
					{}));
				state->rows.push_back(std::move(row));
			}
			list->resizeToWidth(container->width());
		}
		for (auto i = 0; i != int(languages.size()); ++i) {
			state->rows[i]->status = RowStatus(*languages[i]);
		}
		state->changes = ApplicableChanges(*draft);
	};
	refresh();
	draft->updates() | rpl::on_next(refresh, box->lifetime());

	const auto add = [=] {
		const auto chosen = [=](std::vector<LanguageId> ids) {
			auto codes = std::vector<QString>();
			for (const auto id : ids) {
				const auto code = id.twoLetterCode();
				if (Data::ValidBotInfoLangCode(code)) {
					draft->add(code);
					codes.push_back(code);
				}
			}
			if (codes.size() == 1) {
				edit(codes.front());
			}
		};
		box->uiShow()->showBox(Box(
			Ui::ChooseLanguageBox,
			tr::lng_bot_languages_add(),
			chosen,
			std::vector<LanguageId>(),
			true,
			nullptr));
	};
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_bot_languages_add(),
		rpl::single(QString()),
		add,
		st::manageGroupButton,
		{ &st::menuIconAdd }));
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_bot_languages_about(),
			st::boxDividerLabel),
		st::botLanguagesAboutPadding);

	const auto updateStatus = [=] {
		state->status = JobStatus(localization->state());
	};
	state->countdown.setCallback(updateStatus);
	localization->stateValue(
	) | rpl::on_next([=](const JobState &now) {
		updateStatus();
		if (now.pausedTill) {
			state->countdown.callEach(kCountdownStep);
		} else {
			state->countdown.cancel();
		}
	}, box->lifetime());

	const auto apply = [=] {
		const auto count = state->changes.current();
		box->uiShow()->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_bot_languages_apply_sure(
				tr::now,
				lt_count,
				count),
			.confirmed = [=](Fn<void()> close) {
				localization->apply();
				close();
			},
			.confirmText = tr::lng_bot_languages_apply_confirm(),
		}));
	};
	const auto showMenu = [=](not_null<Ui::RpWidget*> button) {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		if (draft->changesCount() > 0) {
			state->menu->addAction(
				tr::lng_bot_languages_discard(tr::now),
				[=] { draft->discard(); },
				&st::menuIconCancel);
		}
		if (state->menu->empty()) {
			state->menu = nullptr;
		} else {
			state->menu->popup(QCursor::pos());
		}
	};
	rpl::combine(
		state->changes.value(),
		localization->stateValue() | rpl::map([](const JobState &state) {
			return (state.job == Job::Apply);
		}) | rpl::distinct_until_changed()
	) | rpl::on_next([=](int changes, bool applying) {
		box->clearButtons();
		if (applying) {
			box->addButton(tr::lng_bot_languages_stop(), [=] {
				localization->cancel();
			});
		} else if (changes > 0) {
			box->addButton(
				tr::lng_bot_languages_apply(
					lt_count,
					rpl::single(changes) | tr::to_count()),
				apply);
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		const auto menuButton = box->addTopButton(st::infoTopBarMenu);
		menuButton->setClickedCallback([=] { showMenu(menuButton); });
	}, box->lifetime());

	if (localization->state().job == Job::None) {
		auto codes = std::vector<QString>{ QString() };
		for (auto &code : KnownLanguageCodes()) {
			codes.push_back(std::move(code));
		}
		codes.erase(
			ranges::remove_if(codes, [&](const QString &code) {
				return localization->known(code);
			}),
			end(codes));
		localization->load(std::move(codes));
	}
}
