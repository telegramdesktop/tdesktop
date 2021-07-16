/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_item_single_media_preview.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "history/history_item.h"
#include "history/view/media/history_view_document.h"
#include "main/main_session.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_player.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

using namespace ::Media::Streaming;

} // namespace

ItemSingleMediaPreview::ItemSingleMediaPreview(
	QWidget *parent,
	Fn<bool()> gifPaused,
	not_null<HistoryItem*> item,
	AttachControls::Type type)
: AbstractSingleMediaPreview(parent, type)
, _gifPaused(std::move(gifPaused))
, _fullId(item->fullId()) {
	const auto media = item->media();
	Assert(media != nullptr);

	Main::Session *session = nullptr;

	if (const auto photo = media->photo()) {
		_photoMedia = photo->createMediaView();
		_photoMedia->wanted(Data::PhotoSize::Large, item->fullId());

		session = &photo->session();
	} else if (const auto document = media->document()) {
		_documentMedia = document->createMediaView();
		_documentMedia->thumbnailWanted(item->fullId());

		session = &document->session();
		if (document->isAnimation() || document->isVideoFile()) {
			setAnimated(true);
			prepareStreamedPreview();
		}
	} else {
		Unexpected("Photo or document should be set.");
	}

	struct ThumbInfo {
		bool loaded = false;
		Image *image = nullptr;
	};

	const auto computeThumbInfo = [=]() -> ThumbInfo {
		using Size = Data::PhotoSize;
		if (_documentMedia) {
			return { true, _documentMedia->thumbnail() };
		} else if (const auto large = _photoMedia->image(Size::Large)) {
			return { true, large };
		} else if (const auto thumbnail = _photoMedia->image(
				Size::Thumbnail)) {
			return { false, thumbnail };
		} else if (const auto small = _photoMedia->image(Size::Small)) {
			return { false, small };
		} else {
			return { false, _photoMedia->thumbnailInline() };
		}
	};

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		session->downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		const auto computed = computeThumbInfo();
		if (!computed.image) {
			if (_documentMedia && !_documentMedia->owner()->hasThumbnail()) {
				const auto size = _documentMedia->owner()->dimensions.scaled(
					st::sendMediaPreviewSize,
					st::confirmMaxHeight,
					Qt::KeepAspectRatio);
				if (!size.isEmpty()) {
					auto empty = QImage(
						size,
						QImage::Format_ARGB32_Premultiplied);
					empty.fill(Qt::black);
					preparePreview(empty);
				}
				_lifetimeDownload.destroy();
			}
			return;
		} else if (computed.loaded) {
			_lifetimeDownload.destroy();
		}
		preparePreview(computed.image->original());
	}, _lifetimeDownload);
}

void ItemSingleMediaPreview::prepareStreamedPreview() {
	if (_streamed || !_documentMedia) {
		return;
	}
	const auto document = _documentMedia
		? _documentMedia->owner().get()
		: nullptr;
	if (document && document->isAnimation()) {
		setupStreamedPreview(
			document->owner().streaming().sharedDocument(
				document,
				_fullId));
	}
}

void ItemSingleMediaPreview::setupStreamedPreview(
		std::shared_ptr<Document> shared) {
	if (!shared) {
		return;
	}
	_streamed = std::make_unique<Instance>(
		std::move(shared),
		[=] { update(); });
	_streamed->lockPlayer();
	_streamed->player().updates(
	) | rpl::start_with_next_error([=](Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->lifetime());

	if (_streamed->ready()) {
		streamingReady(base::duplicate(_streamed->info()));
	}
	checkStreamedIsStarted();
}

void ItemSingleMediaPreview::handleStreamingUpdate(Update &&update) {
	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
	}, [&](const UpdateVideo &update) {
		this->update();
	}, [&](const PreloadedAudio &update) {
	}, [&](const UpdateAudio &update) {
	}, [&](const WaitingForData &update) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
	});
}

void ItemSingleMediaPreview::handleStreamingError(Error &&error) {
}

void ItemSingleMediaPreview::streamingReady(Information &&info) {
}

void ItemSingleMediaPreview::checkStreamedIsStarted() {
	if (!_streamed) {
		return;
	} else if (_streamed->paused()) {
		_streamed->resume();
	}
	if (!_streamed->active() && !_streamed->failed()) {
		startStreamedPlayer();
	}
}

void ItemSingleMediaPreview::startStreamedPlayer() {
	auto options = ::Media::Streaming::PlaybackOptions();
	options.audioId = _documentMedia
		? AudioMsgId(_documentMedia->owner(), _fullId)
		: AudioMsgId();
	options.waitForMarkAsShown = true;
	//if (!_streamed->withSound) {
	options.mode = ::Media::Streaming::Mode::Video;
	options.loop = true;
	//}
	_streamed->play(options);
}

bool ItemSingleMediaPreview::drawBackground() const {
	return true; // A sticker can't be here.
}

bool ItemSingleMediaPreview::tryPaintAnimation(Painter &p) {
	checkStreamedIsStarted();
	if (_streamed
		&& _streamed->player().ready()
		&& !_streamed->player().videoSize().isEmpty()) {
		const auto s = QSize(previewWidth(), previewHeight());
		const auto paused = _gifPaused();

		auto request = ::Media::Streaming::FrameRequest();
		request.outer = s * cIntRetinaFactor();
		request.resize = s * cIntRetinaFactor();
		p.drawImage(
			QRect(
				previewLeft(),
				previewTop(),
				previewWidth(),
				previewHeight()),
			_streamed->frame(request));
		if (!paused) {
			_streamed->markFrameShown();
		}
		return true;
	}
	return false;
}

bool ItemSingleMediaPreview::isAnimatedPreviewReady() const {
	return _streamed != nullptr;
}

auto ItemSingleMediaPreview::sharedPhotoMedia() const
-> std::shared_ptr<::Data::PhotoMedia> {
	return _photoMedia;
}

} // namespace Ui
