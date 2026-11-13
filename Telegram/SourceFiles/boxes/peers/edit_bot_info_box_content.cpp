/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_bot_info_box_content.h"

#include "base/timer.h"
#include "boxes/peers/bot_info_field.h"
#include "boxes/peers/bot_info_language_button.h"
#include "boxes/peers/bot_info_transfer.h"
#include "boxes/peers/translate_bot_info_box.h"
#include "data/data_bot_info_localization.h"
#include "lang/lang_keys.h"
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

#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace {

using Field = Data::BotInfoField;
using Job = Data::BotInfoLocalization::Job;
using JobState = Data::BotInfoLocalization::State;

constexpr auto kCountdownStep = crl::time(500);

[[nodiscard]] rpl::producer<QString> FieldPlaceholder(Field field) {
	switch (field) {
	case Field::Name: return tr::lng_bot_info_name_placeholder();
	case Field::About: return tr::lng_bot_info_about_placeholder();
	case Field::Description: return tr::lng_bot_info_intro_placeholder();
	}
	Unexpected("Field in FieldPlaceholder.");
}

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

[[nodiscard]] QString ProgressText(
		const tr::phrase<lngtag_done, lngtag_total> &phrase,
		const JobState &state) {
	return phrase(
		tr::now,
		lt_done,
		QString::number(state.done),
		lt_total,
		QString::number(state.total));
}

[[nodiscard]] QString JobStatus(const JobState &state) {
	if (!state.stopped.isEmpty()) {
		return tr::lng_bot_languages_stopped(tr::now);
	} else if (state.job != Job::Apply) {
		return QString();
	} else if (state.pausedTill) {
		const auto left = std::max(
			state.pausedTill - crl::now(),
			crl::time(0));
		return tr::lng_bot_languages_paused(
			tr::now,
			lt_time,
			Ui::FormatDurationText((left + 999) / 1000));
	}
	return ProgressText(tr::lng_bot_languages_applying, state);
}

[[nodiscard]] bool Working(const JobState &state) {
	return (state.job == Job::Apply) || (state.job == Job::Translate);
}

} // namespace

void EditBotInfoBoxContent(
		not_null<Ui::GenericBox*> box,
		EditBotInfoBoxArgs &&args) {
	struct State {
		std::array<Ui::InputField*, Data::kBotInfoFields.size()> fields = {};
		rpl::variable<QString> code;
		rpl::variable<QString> intro;
		rpl::variable<QString> status;
		rpl::variable<int> changes;
		rpl::variable<bool> ready;
		base::Timer countdown;
		base::unique_qptr<Ui::PopupMenu> menu;
		Job lastJob = Job::None;
		bool loadWanted = true;
		bool saving = false;
		bool buttonsQueued = false;
	};
	const auto localization = args.localization;
	const auto username = args.username;
	const auto introPreview = args.introPreview;
	const auto addEmojiButton = args.addEmojiButton;
	const auto addAiButton = args.addAiButton;
	const auto chooseImportFile = args.chooseImportFile;
	const auto chooseExportFile = args.chooseExportFile;
	const auto draft = &localization->draft();
	const auto state = box->lifetime().make_state<State>();
	const auto container = box->verticalLayout();

	box->setTitle(tr::lng_manage_peer_bot_edit_info());
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
	const auto loading = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_contacts_loading(),
				st::boxDividerLabel),
			st::botInfoLoadingPadding));

	const auto valueOf = [=](const QString &code, Field field) {
		const auto language = draft->find(code);
		return language ? language->value(field) : QString();
	};
	const auto inheritedValue = [=](Field field) {
		return rpl::combine(
			state->code.value(),
			rpl::single(rpl::empty) | rpl::then(draft->updates())
		) | rpl::map([=](const QString &code, auto) {
			return code.isEmpty() ? QString() : valueOf(QString(), field);
		}) | rpl::distinct_until_changed();
	};
	const auto sync = [=] {
		const auto code = state->code.current();
		for (const auto field : Data::kBotInfoFields) {
			const auto input = state->fields[int(field)];
			const auto value = valueOf(code, field);
			const auto shown = Data::NormalizeBotInfoText(
				field,
				input->getLastText());
			if (shown != value) {
				input->setText(value);
			}
		}
		state->changes = draft->changesCount();
	};
	const auto loadMissing = [=] {
		if (localization->state().job != Job::None) {
			state->loadWanted = true;
			return;
		}
		state->loadWanted = false;
		auto codes = std::vector<QString>{ QString() };
		const auto add = [&](const QString &code) {
			if (!ranges::contains(codes, code)) {
				codes.push_back(code);
			}
		};
		add(state->code.current());
		for (const auto &language : draft->languages()) {
			add(language.code);
		}
		for (const auto &code : KnownLanguageCodes()) {
			add(code);
		}
		codes.erase(
			ranges::remove_if(codes, [&](const QString &code) {
				return localization->known(code);
			}),
			end(codes));
		if (!codes.empty()) {
			localization->load(std::move(codes));
		}
	};
	const auto choose = [=](const QString &code) {
		draft->add(code);
		state->code = code;
		if (!localization->known(code)) {
			loadMissing();
		}
	};

	const auto addButtons = [=](
			Field field,
			not_null<Ui::InputField*> input) {
		const auto withAi = (addAiButton != nullptr);
		if (withAi) {
			addAiButton(input, [=] {
				const auto current = input->getLastText().trimmed();
				return current.isEmpty()
					? valueOf(QString(), field)
					: current;
			});
		}
		const auto reset = Ui::CreateChild<Ui::IconButton>(
			input.get(),
			st::botInfoFieldReset);
		reset->setAccessibleName(tr::lng_bot_info_action_reset(tr::now));
		reset->setClickedCallback([=] {
			input->setText(QString());
			input->setFocus();
		});
		PlaceBotInfoFieldButton(input, reset, withAi ? 2 : 1);
		state->code.value() | rpl::on_next([=](const QString &code) {
			SetBotInfoFieldButtonsCount(
				input,
				1 + (withAi ? 1 : 0) + (code.isEmpty() ? 0 : 1));
		}, reset->lifetime());
		rpl::combine(
			state->code.value(),
			rpl::single(rpl::empty) | rpl::then(input->changes())
		) | rpl::on_next([=](const QString &code, auto) {
			const auto filled = !input->getLastText().isEmpty();
			reset->setVisible(!code.isEmpty() && filled);
		}, reset->lifetime());
	};
	const auto addField = [=](Field field) {
		auto placeholder = rpl::combine(
			inheritedValue(field),
			FieldPlaceholder(field)
		) | rpl::map([](const QString &inherited, const QString &hint) {
			return inherited.isEmpty() ? hint : inherited;
		});
		const auto input = AddBotInfoField(
			container,
			field,
			std::move(placeholder));
		input->setText(valueOf(state->code.current(), field));
		addEmojiButton(input);
		state->fields[int(field)] = input;
		input->changes() | rpl::on_next([=] {
			const auto code = state->code.current();
			const auto text = Data::NormalizeBotInfoText(
				field,
				input->getLastText());
			if (text != valueOf(code, field)) {
				draft->stage(code, field, text);
			}
		}, input->lifetime());
		addButtons(field, input);
		return input;
	};
	const auto fill = [=] {
		loading->hide(anim::type::instant);

		const auto name = addField(Field::Name);
		const auto about = addField(Field::About);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_bot_info_about_caption(),
				st::boxDividerLabel),
			st::botInfoCaptionPadding);

		const auto introText = [=](const QString &text) {
			return text.isEmpty()
				? valueOf(QString(), Field::Description)
				: text;
		};
		state->intro = introText(
			valueOf(state->code.current(), Field::Description));
		container->add(
			introPreview(container, state->intro.value()));
		const auto intro = addField(Field::Description);
		state->intro = rpl::single(rpl::empty) | rpl::then(rpl::merge(
			intro->changes(),
			draft->updates(),
			state->code.changes() | rpl::to_empty
		)) | rpl::map([=] {
			return introText(Data::NormalizeBotInfoText(
				Field::Description,
				intro->getLastText()));
		});
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_bot_info_intro_caption(),
				st::boxDividerLabel),
			st::botInfoCaptionPadding);

		name->submits() | rpl::on_next([=] {
			about->setFocus();
		}, name->lifetime());
		box->setFocusCallback([=] { name->setFocusFast(); });
		name->setFocusFast();

		rpl::merge(
			draft->updates(),
			state->code.changes() | rpl::to_empty
		) | rpl::on_next(sync, box->lifetime());
		sync();
		state->ready = true;
	};

	const auto finishSaving = [=] {
		const auto failed = ranges::find_if(
			draft->languages(),
			[](const Data::BotInfoLanguage &language) {
				return !language.error.isEmpty();
			});
		if (failed != end(draft->languages())) {
			box->showToast(failed->error);
			state->code = failed->code;
		} else if (!draft->changesCount()) {
			box->showToast(tr::lng_bot_info_saved(tr::now));
			box->closeBox();
		}
	};
	const auto showProblem = [=] {
		for (const auto &language : draft->languages()) {
			for (const auto field : Data::kBotInfoFields) {
				if (language.problem(field) != Data::BotInfoProblem::None) {
					state->code = language.code;
					state->fields[int(field)]->showError();
					return true;
				}
			}
		}
		return false;
	};
	const auto apply = [=] {
		state->saving = true;
		localization->apply();
	};
	const auto save = [=] {
		const auto changes = draft->changesCount();
		const auto current = draft->find(state->code.current());
		if (Working(localization->state()) || showProblem()) {
			return;
		} else if (!changes) {
			box->closeBox();
		} else if (current && (current->staged.count() == changes)) {
			apply();
		} else {
			box->uiShow()->showBox(Ui::MakeConfirmBox({
				.text = tr::lng_bot_languages_apply_sure(
					tr::now,
					lt_count,
					changes),
				.confirmed = crl::guard(box, [=](Fn<void()> close) {
					apply();
					close();
				}),
				.confirmText = tr::lng_bot_languages_apply_confirm(),
			}));
		}
	};

	const auto addLanguage = [=] {
		const auto chosen = crl::guard(box, [=](std::vector<LanguageId> ids) {
			const auto code = ids.empty()
				? QString()
				: ids.front().twoLetterCode();
			if (Data::ValidBotInfoLangCode(code)) {
				choose(code);
			}
		});
		box->uiShow()->showBox(Box(
			Ui::ChooseLanguageBox,
			tr::lng_bot_languages_add(),
			chosen,
			std::vector<LanguageId>(),
			false,
			nullptr));
	};
	const auto translated = [=](const QString &single, int count) {
		box->showToast(count
			? tr::lng_bot_languages_translated(tr::now, lt_count, count)
			: tr::lng_bot_languages_translate_failed(tr::now));
		if (count && !single.isEmpty()) {
			choose(single);
		}
	};
	const auto translateTo = [=] {
		if (Working(localization->state())) {
			box->showToast(tr::lng_bot_languages_busy(tr::now));
			return;
		}
		const auto chosen = crl::guard(box, [=](std::vector<LanguageId> ids) {
			auto codes = std::vector<QString>();
			for (const auto id : ids) {
				const auto code = id.twoLetterCode();
				if (Data::ValidBotInfoLangCode(code)) {
					codes.push_back(code);
				}
			}
			if (codes.empty()) {
				return;
			}
			const auto single = (codes.size() == 1)
				? codes.front()
				: QString();
			box->uiShow()->showBox(Box(
				TranslateBotInfoBox,
				localization,
				std::move(codes),
				crl::guard(box, [=](int count) {
					translated(single, count);
				})));
		});
		box->uiShow()->showBox(Box(
			Ui::ChooseLanguagesBox,
			tr::lng_bot_languages_translate_title(),
			tr::lng_bot_languages_translate_button(),
			chosen));
	};
	const auto popup = [=](not_null<Ui::RpWidget*> button) {
		state->menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		state->menu->popup(
			button->mapToGlobal(QPoint(button->width(), button->height())));
	};
	const auto showLanguages = [=](not_null<Ui::RpWidget*> button) {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			box.get(),
			st::botInfoLanguageMenu);
		FillBotInfoLanguageMenu(state->menu.get(), {
			.draft = draft,
			.current = state->code.current(),
			.loading = localization->stateValue(
			) | rpl::map([](const JobState &now) {
				return (now.job == Job::Load)
					? ProgressText(tr::lng_bot_languages_loading, now)
					: QString();
			}),
			.choose = choose,
			.add = addLanguage,
			.translate = translateTo,
		});
		popup(button);
	};
	const auto showMenu = [=](not_null<Ui::RpWidget*> button) {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			box.get(),
			st::popupMenuWithIcons);
		state->menu->addAction(
			tr::lng_bot_languages_import(tr::now),
			[=] {
				chooseImportFile(crl::guard(box, [=](
						QByteArray content,
						QString path) {
					if (!content.isEmpty()) {
						ImportBotInfoContent(
							box,
							localization,
							username,
							content);
					} else if (!path.isEmpty()) {
						ImportBotInfoPath(box, localization, username, path);
					}
				}));
			},
			&st::menuIconImportTheme);
		state->menu->addAction(
			tr::lng_bot_languages_paste(tr::now),
			[=] {
				ImportBotInfoFromClipboard(box, localization, username);
			},
			&st::menuIconCopy);
		state->menu->addAction(
			tr::lng_bot_languages_export(tr::now),
			[=] {
				chooseExportFile(
					username + u"_languages.json"_q,
					crl::guard(box, [=](QString path) {
						ExportBotInfoToPath(
							box,
							localization,
							username,
							path);
					}));
			},
			&st::menuIconExportTheme);
		if (draft->changesCount() > 0) {
			state->menu->addAction(
				tr::lng_bot_languages_discard(tr::now),
				[=] { draft->discard(); },
				&st::menuIconCancel);
		}
		popup(button);
	};
	const auto refreshButtons = [=] {
		state->buttonsQueued = false;
		box->clearButtons();
		if (Working(localization->state())) {
			box->addButton(tr::lng_bot_languages_stop(), [=] {
				localization->cancel();
			});
		} else if (state->changes.current() > 0) {
			box->addButton(tr::lng_settings_save(), save);
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		const auto menu = box->addTopButton(st::botInfoMenu);
		menu->setClickedCallback([=] { showMenu(menu); });
		if (state->ready.current()) {
			const auto language = box->addTopButton(
				CreateBotInfoLanguageButton(
					state->code.value() | rpl::map(BotInfoLanguageName)));
			language->setClickedCallback([=] { showLanguages(language); });
		}
	};
	AcceptBotInfoFileDrops(box, localization, username);
	refreshButtons();
	rpl::merge(
		state->changes.changes() | rpl::map([](int changes) {
			return (changes > 0);
		}) | rpl::distinct_until_changed() | rpl::to_empty,
		state->ready.changes() | rpl::to_empty,
		localization->stateValue(
		) | rpl::map(Working) | rpl::distinct_until_changed(
		) | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([=] {
		if (!std::exchange(state->buttonsQueued, true)) {
			crl::on_main(box, refreshButtons);
		}
	}, box->lifetime());

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
		const auto finished = std::exchange(state->lastJob, now.job);
		if (!state->ready.current() && localization->known(QString())) {
			fill();
		}
		if (now.job != Job::None) {
			return;
		} else if (state->loadWanted
			|| (finished == Job::Apply)
			|| (finished == Job::Translate)) {
			crl::on_main(box, [=] {
				if (localization->state().stopped.isEmpty()) {
					loadMissing();
				}
			});
		}
		if ((finished == Job::Apply) && base::take(state->saving)) {
			finishSaving();
		}
	}, box->lifetime());
}
