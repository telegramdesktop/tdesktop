/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_bot_languages_box.h"

#include "api/api_bot_info.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "boxes/peers/bot_info_file.h"
#include "boxes/peers/edit_bot_info_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/translate_bot_info_box.h"
#include "core/file_utilities.h"
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

#include <QtCore/QFile>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QDropEvent>
#include <QtGui/QGuiApplication>

namespace {

using Job = Api::BotInfoLocalization::Job;
using JobState = Api::BotInfoLocalization::State;

constexpr auto kCountdownStep = crl::time(500);
constexpr auto kMaxFileSize = 1024 * 1024;

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
		return language.hasTranslated()
			? tr::lng_bot_languages_translated(tr::now)
			: tr::lng_bot_languages_edited(tr::now);
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
	case Job::Translate:
		return tr::lng_bot_languages_translating(
			tr::now,
			lt_done,
			done,
			lt_total,
			total);
	case Job::None: break;
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

[[nodiscard]] QString FileErrorText(const BotInfoFile &file) {
	switch (file.error) {
	case BotInfoFileError::TooLarge:
		return tr::lng_bot_languages_file_large(tr::now);
	case BotInfoFileError::Syntax:
		return tr::lng_bot_languages_file_syntax(
			tr::now,
			lt_line,
			QString::number(file.line),
			lt_column,
			QString::number(file.column),
			lt_error,
			file.details);
	case BotInfoFileError::Shape:
		return tr::lng_bot_languages_file_shape(tr::now);
	case BotInfoFileError::UnknownKey:
		return tr::lng_bot_languages_file_key(tr::now, lt_key, file.where);
	case BotInfoFileError::NotString:
		return tr::lng_bot_languages_file_string(
			tr::now,
			lt_key,
			file.where);
	case BotInfoFileError::BadCode:
		return tr::lng_bot_languages_file_code(
			tr::now,
			lt_code,
			file.where);
	case BotInfoFileError::Duplicate:
		return tr::lng_bot_languages_file_duplicate(
			tr::now,
			lt_key,
			file.where);
	case BotInfoFileError::None: break;
	}
	return QString();
}

[[nodiscard]] QString ImportedText(
		const BotInfoFile &file,
		const Data::BotInfoDraft &draft) {
	auto changes = 0;
	auto problems = 0;
	for (const auto &entry : file.languages) {
		const auto language = draft.find(entry.code);
		if (!language) {
			continue;
		}
		for (const auto field : Data::kBotInfoFields) {
			if (entry.texts.field(field) && language->staged.field(field)) {
				++changes;
			}
		}
		if (language->hasProblem()) {
			++problems;
		}
	}
	if (!changes) {
		return tr::lng_bot_languages_imported_none(tr::now);
	}
	const auto imported = tr::lng_bot_languages_imported(
		tr::now,
		lt_count,
		changes);
	return !problems
		? imported
		: (imported + u' ' + tr::lng_bot_languages_imported_problems(
			tr::now,
			lt_count,
			problems));
}

[[nodiscard]] QString DroppedFile(not_null<const QMimeData*> data) {
	const auto urls = data->urls();
	const auto path = (urls.size() == 1)
		? urls.front().toLocalFile()
		: QString();
	return path.endsWith(u".json"_q, Qt::CaseInsensitive) ? path : QString();
}

void ApplyFile(
		not_null<Ui::GenericBox*> box,
		not_null<Api::BotInfoLocalization*> localization,
		const BotInfoFile &file) {
	const auto draft = &localization->draft();
	auto unknown = std::vector<QString>();
	for (const auto &entry : file.languages) {
		for (const auto field : Data::kBotInfoFields) {
			if (const auto &text = entry.texts.field(field)) {
				draft->stage(entry.code, field, *text);
			}
		}
		if (!localization->known(entry.code)) {
			unknown.push_back(entry.code);
		}
	}
	if (!unknown.empty() && (localization->state().job == Job::None)) {
		localization->load(std::move(unknown));
	}
	box->showToast(ImportedText(file, *draft));
}

void ImportContent(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot,
		const QByteArray &content) {
	const auto localization
		= bot->session().api().botInfoLocalizations().get(bot);
	const auto file = ParseBotInfoFile(content);
	const auto owner = file.bot.startsWith(u'@') ? file.bot.mid(1) : file.bot;
	if (file.error != BotInfoFileError::None) {
		box->uiShow()->showBox(Ui::MakeInformBox(FileErrorText(file)));
	} else if (owner.isEmpty()
		|| !owner.compare(bot->username(), Qt::CaseInsensitive)) {
		ApplyFile(box, localization, file);
	} else {
		box->uiShow()->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_bot_languages_file_other_bot(
				tr::now,
				lt_bot,
				owner),
			.confirmed = crl::guard(box, [=](Fn<void()> close) {
				ApplyFile(box, localization, file);
				close();
			}),
			.confirmText = tr::lng_bot_languages_file_other_bot_confirm(),
		}));
	}
}

void ImportPath(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot,
		const QString &path) {
	auto file = QFile(path);
	if (file.size() > kMaxFileSize) {
		box->showToast(tr::lng_bot_languages_file_large(tr::now));
	} else if (!file.open(QIODevice::ReadOnly)) {
		box->showToast(tr::lng_bot_languages_file_read(tr::now));
	} else {
		ImportContent(box, bot, file.readAll());
	}
}

void ImportFromFile(not_null<Ui::GenericBox*> box, not_null<UserData*> bot) {
	FileDialog::GetOpenPath(
		box.get(),
		tr::lng_bot_languages_import(tr::now),
		u"JSON files (*.json);;"_q + FileDialog::AllFilesFilter(),
		crl::guard(box, [=](FileDialog::OpenResult &&result) {
			if (!result.remoteContent.isEmpty()) {
				ImportContent(box, bot, result.remoteContent);
			} else if (!result.paths.isEmpty()) {
				ImportPath(box, bot, result.paths.front());
			}
		}));
}

void ExportToFile(not_null<Ui::GenericBox*> box, not_null<UserData*> bot) {
	const auto draft = &bot->session().api().botInfoLocalizations().get(
		bot)->draft();
	FileDialog::GetWritePath(
		box.get(),
		tr::lng_bot_languages_export(tr::now),
		u"JSON files (*.json)"_q,
		bot->username() + u"_languages.json"_q,
		crl::guard(box, [=](const QString &path) {
			auto file = QFile(path);
			const auto content = SerializeBotInfoFile(
				bot->username(),
				*draft);
			const auto saved = file.open(QIODevice::WriteOnly)
				&& (file.write(content) == content.size());
			box->showToast(saved
				? tr::lng_bot_languages_exported(tr::now)
				: tr::lng_bot_languages_file_write(tr::now));
		}));
}

void AcceptDroppedFiles(
		not_null<Ui::GenericBox*> box,
		not_null<UserData*> bot) {
	box->setAcceptDrops(true);
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if ((type != QEvent::DragEnter)
			&& (type != QEvent::DragMove)
			&& (type != QEvent::Drop)) {
			return base::EventFilterResult::Continue;
		}
		const auto drop = static_cast<QDropEvent*>(e.get());
		const auto path = DroppedFile(drop->mimeData());
		if (path.isEmpty()) {
			return base::EventFilterResult::Continue;
		}
		drop->acceptProposedAction();
		if (type == QEvent::Drop) {
			ImportPath(box, bot, path);
		}
		return base::EventFilterResult::Cancel;
	});
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
	container->add(EditPeerInfoBox::CreateButton(
		container,
		tr::lng_bot_languages_translate(),
		rpl::single(QString()),
		[=] {
			if (localization->state().job != Job::None) {
				box->showToast(tr::lng_bot_languages_busy(tr::now));
			} else {
				box->uiShow()->showBox(Box(TranslateBotInfoBox, bot));
			}
		},
		st::manageGroupButton,
		{ &st::menuIconTranslate }));
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
		state->menu->addAction(
			tr::lng_bot_languages_import(tr::now),
			[=] { ImportFromFile(box, bot); },
			&st::menuIconImportTheme);
		state->menu->addAction(
			tr::lng_bot_languages_paste(tr::now),
			[=] {
				const auto text = QGuiApplication::clipboard()->text();
				ImportContent(box, bot, text.toUtf8());
			},
			&st::menuIconCopy);
		state->menu->addAction(
			tr::lng_bot_languages_export(tr::now),
			[=] { ExportToFile(box, bot); },
			&st::menuIconExportTheme);
		if (draft->changesCount() > 0) {
			state->menu->addAction(
				tr::lng_bot_languages_discard(tr::now),
				[=] { draft->discard(); },
				&st::menuIconCancel);
		}
		state->menu->popup(QCursor::pos());
	};
	AcceptDroppedFiles(box, bot);
	rpl::combine(
		state->changes.value(),
		localization->stateValue() | rpl::map([](const JobState &state) {
			return (state.job == Job::Apply)
				|| (state.job == Job::Translate);
		}) | rpl::distinct_until_changed()
	) | rpl::on_next([=](int changes, bool working) {
		box->clearButtons();
		if (working) {
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
