/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/message_video_edit.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "editor/video/video_clip.h"
#include "editor/video/video_editor_layer.h"
#include "editor/editor_message_video.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"

DocumentData *FindEditableMessageVideo(HistoryItem *item) {
	const auto media = item
		&& item->allowsEditMedia()
		&& item->canBeEdited()
			? item->media()
			: nullptr;
	const auto document = (media
		&& media->allowsEditMedia()
		&& !media->webpage())
		? media->document()
		: nullptr;
	return (document
		&& document->isVideoFile()
		&& document->size > 0
		&& document->size < Media::Encode::MaxTranscodeSourceSize())
		? document
		: nullptr;
}

void EditMessageVideo(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		Fn<void(Ui::PreparedList&&)> done) {
	const auto &data = controller->session().data();
	const auto item = data.message(itemId);
	const auto document = FindEditableMessageVideo(item);
	if (!document) {
		return;
	}
	const auto allowGif = item->groupId().empty();
	const auto video = std::make_shared<Editor::MessageVideo>(
		document,
		itemId,
		Media::Encode::MaxTranscodeSourceSize());
	const auto current = [=] {
		return FindEditableMessageVideo(
			controller->session().data().message(itemId))
				== document;
	};
	auto openEditor = [=] {
		if (!current()) {
			return;
		}
		const auto source = video->source();
		if (!source) {
			controller->showToast(
				tr::lng_edit_media_invalid_file(tr::now));
			return;
		}
		const auto list = std::make_shared<Ui::PreparedList>();
		auto &file = list->files.emplace_back(source->path);
		file.displayName = document->filename();
		file.content = source->content;
		file.size = document->size;
		file.temporaryPath = video->temporaryPath();
		file.type = Ui::PreparedFile::Type::Video;
		file.information
			= std::make_unique<Ui::PreparedFileInformation>();
		file.information->filemime = document->mimeString();
		file.information->media = Ui::PreparedFileInformation::Video{
			.supportsStreaming = document->supportsStreaming(),
			.hasAudio = source->hasAudio,
			.duration = source->duration,
			.thumbnail = source->thumbnail,
		};
		const auto dimensions = source->thumbnail.size();
		const auto duration = source->duration;
		auto confirmEdit = [=](bool accepted) {
			if (!accepted || !current()) {
				return;
			}
			using Video = Ui::PreparedFileInformation::Video;
			const auto &info = std::get<Video>(
				list->files.front().information->media);
			if (info.modifications.cover
				|| Editor::VideoEdited(
					info.modifications,
					dimensions,
					duration,
					true)) {
				done(std::move(*list));
			}
		};
		Editor::OpenWithPreparedVideoFile(
			controller->content(),
			controller->uiShow(),
			list.get(),
			0,
			st::sendMediaPreviewSize,
			crl::guard(controller, std::move(confirmEdit)),
			PhotoSideLimit(true),
			allowGif);
	};
	const auto open = crl::guard(controller, std::move(openEditor));
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_context_edit_video());
		auto progress = video->changes() | rpl::map([=] {
			const auto percent = int(
				base::SafeRound(video->progress() * 100));
			return tr::lng_send_video_preparing(
				tr::now,
				lt_progress,
				QString::number(percent));
		});
		auto label = rpl::single(
			tr::lng_contacts_loading(tr::now)
		) | rpl::then(
			std::move(progress)
		) | rpl::distinct_until_changed();
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(label),
				st::boxLabel));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		const auto active = box->lifetime().make_state<bool>(true);
		box->boxClosing() | rpl::on_next([=] {
			*active = false;
		}, box->lifetime());
		controller->session().data().itemRemoved(
			itemId
		) | rpl::on_next([=] { box->closeBox(); }, box->lifetime());
		video->changes(
		) | rpl::filter([=] {
			return *active && !video->loading();
		}) | rpl::take(1) | rpl::on_next([=] {
			InvokeQueued(
				box,
				[=] {
					if (*active) {
						box->closeBox();
						open();
					}
				});
		}, box->lifetime());
		InvokeQueued(
			box,
			[=] {
				if (*active) {
					video->load();
				}
			});
	}));
}
