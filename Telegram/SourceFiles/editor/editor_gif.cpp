/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_gif.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "media/clip/media_clip_reader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/rp_widget.h"

namespace Editor {

void LoadGif(
		not_null<Ui::RpWidget*> context,
		not_null<DocumentData*> document,
		Fn<void(Storage::PhotoEditorMedia&&)> done) {
	const auto media = document->createMediaView();
	const auto deliver = crl::guard(context, done);
	const auto finish = [=] {
		const auto content = media->bytes();
		const auto path = content.isEmpty()
			? document->filepath(true)
			: QString();
		if (content.isEmpty() && path.isEmpty()) {
			if (!document->cancelled()) {
				done({});
			}
			return;
		}
		crl::async([=] {
			auto prepared
				= Media::Clip::PrepareForSending(
					path,
					content);
			using Video = Ui::PreparedFileInformation::Video;
			auto video = std::get_if<Video>(&prepared.media);
			auto result = Storage::PhotoEditorMedia();
			if (video && video->duration > 0) {
				result = {
					.image = std::move(video->thumbnail),
					.videoPath = path,
					.videoContent = content,
					.videoDuration = video->duration,
				};
			}
			crl::on_main([=, result = std::move(result)](
				) mutable {
				deliver(std::move(result));
			});
		});
	};
	if (!media->loaded(true) && !document->loading()) {
		document->save(document->stickerOrGifOrigin(), QString());
	}
	if (!document->loading()) {
		finish();
		return;
	}
	document->owner().documentLoadProgress(
	) | rpl::filter([=](not_null<DocumentData*> updated) {
		return (updated == document) && !document->loading();
	}) | rpl::take(1) | rpl::on_next(finish, context->lifetime());
}

} // namespace Editor
