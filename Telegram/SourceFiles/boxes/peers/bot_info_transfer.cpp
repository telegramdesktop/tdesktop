/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_info_transfer.h"

#include "base/event_filter.h"
#include "boxes/peers/bot_info_file.h"
#include "data/data_bot_info_localization.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"

#include <QtCore/QFile>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QDropEvent>
#include <QtGui/QGuiApplication>

namespace {

using Job = Data::BotInfoLocalization::Job;

constexpr auto kMaxFileSize = 1024 * 1024;

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
		not_null<Data::BotInfoLocalization*> localization,
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

} // namespace

void ImportBotInfoContent(
		not_null<Ui::GenericBox*> box,
		not_null<Data::BotInfoLocalization*> localization,
		const QString &username,
		const QByteArray &content) {
	const auto file = ParseBotInfoFile(content);
	const auto owner = file.bot.startsWith(u'@') ? file.bot.mid(1) : file.bot;
	if (file.error != BotInfoFileError::None) {
		box->uiShow()->showBox(Ui::MakeInformBox(FileErrorText(file)));
	} else if (owner.isEmpty()
		|| !owner.compare(username, Qt::CaseInsensitive)) {
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

void ImportBotInfoPath(
		not_null<Ui::GenericBox*> box,
		not_null<Data::BotInfoLocalization*> localization,
		const QString &username,
		const QString &path) {
	auto file = QFile(path);
	if (file.size() > kMaxFileSize) {
		box->showToast(tr::lng_bot_languages_file_large(tr::now));
	} else if (!file.open(QIODevice::ReadOnly)) {
		box->showToast(tr::lng_bot_languages_file_read(tr::now));
	} else {
		ImportBotInfoContent(box, localization, username, file.readAll());
	}
}

void ImportBotInfoFromClipboard(
		not_null<Ui::GenericBox*> box,
		not_null<Data::BotInfoLocalization*> localization,
		const QString &username) {
	const auto text = QGuiApplication::clipboard()->text();
	ImportBotInfoContent(box, localization, username, text.toUtf8());
}

void ExportBotInfoToPath(
		not_null<Ui::GenericBox*> box,
		not_null<Data::BotInfoLocalization*> localization,
		const QString &username,
		const QString &path) {
	auto file = QFile(path);
	const auto content = SerializeBotInfoFile(
		username,
		localization->draft());
	const auto saved = file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size());
	box->showToast(saved
		? tr::lng_bot_languages_exported(tr::now)
		: tr::lng_bot_languages_file_write(tr::now));
}

void AcceptBotInfoFileDrops(
		not_null<Ui::GenericBox*> box,
		not_null<Data::BotInfoLocalization*> localization,
		const QString &username) {
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
			ImportBotInfoPath(box, localization, username, path);
		}
		return base::EventFilterResult::Cancel;
	});
}
