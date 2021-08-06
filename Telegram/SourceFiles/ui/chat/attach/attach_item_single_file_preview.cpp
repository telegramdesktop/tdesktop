/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_item_single_file_preview.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "history/history_item.h"
#include "history/view/media/history_view_document.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/format_song_name.h"
#include "ui/text/format_values.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

AttachControls::Type CheckControlsType(
		not_null<HistoryItem*> item,
		AttachControls::Type type) {
	const auto media = item->media();
	Assert(media != nullptr);
	return media->allowsEditMedia()
		? type
		: AttachControls::Type::None;
}

} // namespace

ItemSingleFilePreview::ItemSingleFilePreview(
	QWidget *parent,
	not_null<HistoryItem*> item,
	AttachControls::Type type)
: AbstractSingleFilePreview(parent, CheckControlsType(item, type)) {
	const auto media = item->media();
	Assert(media != nullptr);
	const auto document = media->document();
	Assert(document != nullptr);

	_documentMedia = document->createMediaView();
	_documentMedia->thumbnailWanted(item->fullId());

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		document->session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (_documentMedia->thumbnail()) {
			_lifetimeDownload.destroy();
		}
		preparePreview(document);
	}, _lifetimeDownload);
}

void ItemSingleFilePreview::preparePreview(not_null<DocumentData*> document) {
	AbstractSingleFilePreview::Data data;

	const auto preview = _documentMedia->thumbnail()
		? _documentMedia->thumbnail()->original()
		: QImage();

	prepareThumbFor(data, preview);
	data.fileIsImage = document->isImage();
	data.fileIsAudio = document->isAudioFile() || document->isVoiceMessage();

	if (data.fileIsImage) {
		data.name = document->filename();
		// data.statusText = FormatImageSizeText(preview.size()
		// 	/ preview.devicePixelRatio());
	} else if (data.fileIsAudio) {
		auto filename = document->filename();

		auto songTitle = QString();
		auto songPerformer = QString();
		if (const auto song = document->song()) {
			songTitle = song->title;
			songPerformer = song->performer;

			if (document->isSongWithCover()) {
				const auto size = QSize(
					st::attachPreviewLayout.thumbSize,
					st::attachPreviewLayout.thumbSize);
				auto thumb = QPixmap(size);
				thumb.fill(Qt::transparent);
				Painter p(&thumb);

				HistoryView::DrawThumbnailAsSongCover(
					p,
					_documentMedia,
					QRect(QPoint(), size));
				data.fileThumb = std::move(thumb);
			}
		} else if (document->isVoiceMessage()) {
			songTitle = tr::lng_media_audio(tr::now);
		}

		data.name = Text::FormatSongName(filename, songTitle, songPerformer)
			.string();
		data.statusText = FormatSizeText(document->size);
	} else {
		data.name = document->filename();
	}
	data.statusText = FormatSizeText(document->size);

	setData(data);
}

} // namespace Ui
