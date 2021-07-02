/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document_media.h"

#include "data/data_document.h"
#include "data/data_document_resolver.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_file_origin.h"
#include "data/data_auto_download.h"
#include "media/clip/media_clip_reader.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "lottie/lottie_animation.h"
#include "history/history_item.h"
#include "history/history.h"
#include "window/themes/window_theme_preview.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "storage/file_download.h"
#include "ui/image/image.h"
#include "app.h"

#include <QtCore/QBuffer>
#include <QtGui/QImageReader>

namespace Data {
namespace {

constexpr auto kReadAreaLimit = 12'032 * 9'024;
constexpr auto kWallPaperThumbnailLimit = 960;
constexpr auto kGoodThumbQuality = 87;

enum class FileType {
	Video,
	AnimatedSticker,
	WallPaper,
	Theme,
};

[[nodiscard]] bool MayHaveGoodThumbnail(not_null<DocumentData*> owner) {
	return owner->isVideoFile()
		|| owner->isAnimation()
		|| owner->isWallPaper()
		|| owner->isTheme()
		|| (owner->sticker() && owner->sticker()->animated);
}

[[nodiscard]] QImage PrepareGoodThumbnail(
		const QString &path,
		QByteArray data,
		FileType type) {
	if (type == FileType::Video) {
		return ::Media::Clip::PrepareForSending(path, data).thumbnail;
	} else if (type == FileType::AnimatedSticker) {
		return Lottie::ReadThumbnail(Lottie::ReadContent(data, path));
	} else if (type == FileType::Theme) {
		return Window::Theme::GeneratePreview(data, path);
	}
	auto buffer = QBuffer(&data);
	auto file = QFile(path);
	auto device = data.isEmpty() ? static_cast<QIODevice*>(&file) : &buffer;
	auto reader = QImageReader(device);
	const auto size = reader.size();
	if (!reader.canRead()
		|| (size.width() * size.height() > kReadAreaLimit)) {
		return QImage();
	}
	auto result = reader.read();
	if (!result.width() || !result.height()) {
		return QImage();
	}
	return (result.width() > kWallPaperThumbnailLimit
		|| result.height() > kWallPaperThumbnailLimit)
		? result.scaled(
			kWallPaperThumbnailLimit,
			kWallPaperThumbnailLimit,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: result;
}

} // namespace

VideoPreviewState::VideoPreviewState(DocumentMedia *media)
: _media(media)
, _usingThumbnail(media ? media->owner()->hasVideoThumbnail() : false) {
}

void VideoPreviewState::automaticLoad(Data::FileOrigin origin) const {
	Expects(_media != nullptr);

	if (_usingThumbnail) {
		_media->videoThumbnailWanted(origin);
	} else {
		_media->automaticLoad(origin, nullptr);
	}
}

::Media::Clip::ReaderPointer VideoPreviewState::makeAnimation(
		Fn<void(::Media::Clip::Notification)> callback) const {
	Expects(_media != nullptr);
	Expects(loaded());

	return _usingThumbnail
		? ::Media::Clip::MakeReader(
			_media->videoThumbnailContent(),
			std::move(callback))
		: ::Media::Clip::MakeReader(
			_media->owner()->location(),
			_media->bytes(),
			std::move(callback));
}

bool VideoPreviewState::usingThumbnail() const {
	return _usingThumbnail;
}

bool VideoPreviewState::loading() const {
	return _usingThumbnail
		? _media->owner()->videoThumbnailLoading()
		: _media
		? _media->owner()->loading()
		: false;
}

bool VideoPreviewState::loaded() const {
	return _usingThumbnail
		? !_media->videoThumbnailContent().isEmpty()
		: _media
		? _media->loaded()
		: false;
}

DocumentMedia::DocumentMedia(not_null<DocumentData*> owner)
: _owner(owner) {
}

// NB! Right now DocumentMedia can outlive Main::Session!
// In DocumentData::collectLocalData a shared_ptr is sent on_main.
// In case this is a problem the ~Gif code should be rewritten.
DocumentMedia::~DocumentMedia() = default;

not_null<DocumentData*> DocumentMedia::owner() const {
	return _owner;
}

void DocumentMedia::goodThumbnailWanted() {
	_flags |= Flag::GoodThumbnailWanted;
}

Image *DocumentMedia::goodThumbnail() const {
	Expects((_flags & Flag::GoodThumbnailWanted) != 0);

	if (!_goodThumbnail) {
		ReadOrGenerateThumbnail(_owner);
	}
	return _goodThumbnail.get();
}

void DocumentMedia::setGoodThumbnail(QImage thumbnail) {
	if (!(_flags & Flag::GoodThumbnailWanted)) {
		return;
	}
	_goodThumbnail = std::make_unique<Image>(std::move(thumbnail));
	_owner->session().notifyDownloaderTaskFinished();
}

Image *DocumentMedia::thumbnailInline() const {
	if (!_inlineThumbnail && !_owner->inlineThumbnailIsPath()) {
		const auto bytes = _owner->inlineThumbnailBytes();
		if (!bytes.isEmpty()) {
			auto image = Images::FromInlineBytes(bytes);
			if (image.isNull()) {
				_owner->clearInlineThumbnailBytes();
			} else {
				_inlineThumbnail = std::make_unique<Image>(std::move(image));
			}
		}
	}
	return _inlineThumbnail.get();
}

const QPainterPath &DocumentMedia::thumbnailPath() const {
	if (_pathThumbnail.isEmpty() && _owner->inlineThumbnailIsPath()) {
		const auto bytes = _owner->inlineThumbnailBytes();
		if (!bytes.isEmpty()) {
			_pathThumbnail = Images::PathFromInlineBytes(bytes);
			if (_pathThumbnail.isEmpty()) {
				_owner->clearInlineThumbnailBytes();
			}
		}
	}
	return _pathThumbnail;
}

Image *DocumentMedia::thumbnail() const {
	return _thumbnail.get();
}

void DocumentMedia::thumbnailWanted(Data::FileOrigin origin) {
	if (!_thumbnail) {
		_owner->loadThumbnail(origin);
	}
}

QSize DocumentMedia::thumbnailSize() const {
	if (const auto image = _thumbnail.get()) {
		return image->size();
	}
	const auto &location = _owner->thumbnailLocation();
	return { location.width(), location.height() };
}

void DocumentMedia::setThumbnail(QImage thumbnail) {
	_thumbnail = std::make_unique<Image>(std::move(thumbnail));
	_owner->session().notifyDownloaderTaskFinished();
}

QByteArray DocumentMedia::videoThumbnailContent() const {
	return _videoThumbnailBytes;
}

QSize DocumentMedia::videoThumbnailSize() const {
	const auto &location = _owner->videoThumbnailLocation();
	return { location.width(), location.height() };
}

void DocumentMedia::videoThumbnailWanted(Data::FileOrigin origin) {
	if (_videoThumbnailBytes.isEmpty()) {
		_owner->loadVideoThumbnail(origin);
	}
}

void DocumentMedia::setVideoThumbnail(QByteArray content) {
	_videoThumbnailBytes = std::move(content);
}

void DocumentMedia::checkStickerLarge() {
	if (_sticker) {
		return;
	}
	const auto data = _owner->sticker();
	if (!data) {
		return;
	}
	automaticLoad(_owner->stickerSetOrigin(), nullptr);
	if (data->animated || !loaded()) {
		return;
	}
	if (_bytes.isEmpty()) {
		const auto &loc = _owner->location(true);
		if (loc.accessEnable()) {
			_sticker = std::make_unique<Image>(loc.name());
			loc.accessDisable();
		}
	} else {
		_sticker = std::make_unique<Image>(_bytes);
	}
}

void DocumentMedia::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (_owner->status != FileReady || loaded() || _owner->cancelled()) {
		return;
	} else if (!item && !_owner->sticker() && !_owner->isAnimation()) {
		return;
	}
	const auto toCache = _owner->saveToCache();
	if (!toCache && Core::App().settings().askDownloadPath()) {
		// We need a filename, but we're supposed to ask user for it.
		// No automatic download in this case.
		return;
	}
	const auto filename = toCache
		? QString()
		: DocumentFileNameForSave(_owner);
	const auto shouldLoadFromCloud = !Data::IsExecutableName(filename)
		&& (item
			? Data::AutoDownload::Should(
				_owner->session().settings().autoDownload(),
				item->history()->peer,
				_owner)
			: Data::AutoDownload::Should(
				_owner->session().settings().autoDownload(),
				_owner));
	const auto loadFromCloud = shouldLoadFromCloud
		? LoadFromCloudOrLocal
		: LoadFromLocalOnly;
	_owner->save(
		origin,
		filename,
		loadFromCloud,
		true);
}

void DocumentMedia::collectLocalData(not_null<DocumentMedia*> local) {
	if (const auto image = local->_goodThumbnail.get()) {
		_goodThumbnail = std::make_unique<Image>(image->original());
	}
	if (const auto image = local->_inlineThumbnail.get()) {
		_inlineThumbnail = std::make_unique<Image>(image->original());
	}
	if (const auto image = local->_thumbnail.get()) {
		_thumbnail = std::make_unique<Image>(image->original());
	}
	if (const auto image = local->_sticker.get()) {
		_sticker = std::make_unique<Image>(image->original());
	}
	_bytes = local->_bytes;
	_videoThumbnailBytes = local->_videoThumbnailBytes;
	_flags = local->_flags;
}

void DocumentMedia::setBytes(const QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		_bytes = bytes;
	}
}

QByteArray DocumentMedia::bytes() const {
	return _bytes;
}

bool DocumentMedia::loaded(bool check) const {
	return !_bytes.isEmpty() || !_owner->filepath(check).isEmpty();
}

float64 DocumentMedia::progress() const {
	return (_owner->uploading() || _owner->loading())
		? _owner->progress()
		: (loaded() ? 1. : 0.);
}

bool DocumentMedia::canBePlayed() const {
	return !_owner->inappPlaybackFailed()
		&& _owner->useStreamingLoader()
		&& (loaded() || _owner->canBeStreamed());
}

bool DocumentMedia::thumbnailEnoughForSticker() const {
	const auto &location = owner()->thumbnailLocation();
	const auto size = _thumbnail
		? QSize(_thumbnail->width(), _thumbnail->height())
		: location.valid()
		? QSize(location.width(), location.height())
		: QSize();
	return (size.width() >= 128) || (size.height() >= 128);
}

void DocumentMedia::checkStickerSmall() {
	const auto data = _owner->sticker();
	if ((data && data->animated) || thumbnailEnoughForSticker()) {
		_owner->loadThumbnail(_owner->stickerSetOrigin());
		if (data && data->animated) {
			automaticLoad(_owner->stickerSetOrigin(), nullptr);
		}
	} else {
		checkStickerLarge();
	}
}

Image *DocumentMedia::getStickerLarge() {
	checkStickerLarge();
	return _sticker.get();
}

Image *DocumentMedia::getStickerSmall() {
	const auto data = _owner->sticker();
	if ((data && data->animated) || thumbnailEnoughForSticker()) {
		return thumbnail();
	}
	return _sticker.get();
}

void DocumentMedia::checkStickerLarge(not_null<FileLoader*> loader) {
	if (_sticker || !_owner->sticker()) {
		return;
	}
	if (auto image = loader->imageData(); !image.isNull()) {
		_sticker = std::make_unique<Image>(std::move(image));
	}
}

void DocumentMedia::GenerateGoodThumbnail(
		not_null<DocumentData*> document,
		QByteArray data) {
	const auto type = document->isWallPaper()
		? FileType::WallPaper
		: document->isTheme()
		? FileType::Theme
		: document->sticker()
		? FileType::AnimatedSticker
		: FileType::Video;
	auto location = document->location().isEmpty()
		? nullptr
		: std::make_unique<Core::FileLocation>(document->location());
	if (data.isEmpty() && !location) {
		document->setGoodThumbnailChecked(false);
		return;
	}
	const auto guard = base::make_weak(&document->owner().session());
	crl::async([=, location = std::move(location)] {
		const auto filepath = (location && location->accessEnable())
			? location->name()
			: QString();
		auto result = PrepareGoodThumbnail(filepath, data, type);
		auto bytes = QByteArray();
		if (!result.isNull()) {
			auto buffer = QBuffer(&bytes);
			const auto format = (type == FileType::AnimatedSticker)
				? "WEBP"
				: (type == FileType::WallPaper && result.hasAlphaChannel())
				? "PNG"
				: "JPG";
			result.save(&buffer, format, kGoodThumbQuality);
		}
		if (!filepath.isEmpty()) {
			location->accessDisable();
		}
		const auto cache = bytes.isEmpty() ? QByteArray("(failed)") : bytes;
		crl::on_main(guard, [=] {
			document->setGoodThumbnailChecked(true);
			if (const auto active = document->activeMediaView()) {
				active->setGoodThumbnail(result);
			}
			document->owner().cache().put(
				document->goodThumbnailCacheKey(),
				Storage::Cache::Database::TaggedValue{
					base::duplicate(cache),
					kImageCacheTag });
		});
	});
}

void DocumentMedia::CheckGoodThumbnail(not_null<DocumentData*> document) {
	if (!document->goodThumbnailChecked()) {
		ReadOrGenerateThumbnail(document);
	}
}

void DocumentMedia::ReadOrGenerateThumbnail(
		not_null<DocumentData*> document) {
	if (document->goodThumbnailGenerating()
		|| document->goodThumbnailNoData()
		|| !MayHaveGoodThumbnail(document)) {
		return;
	}
	document->setGoodThumbnailGenerating();

	const auto guard = base::make_weak(&document->session());
	const auto active = document->activeMediaView();
	const auto got = [=](QByteArray value) {
		if (value.isEmpty()) {
			const auto bytes = active ? active->bytes() : QByteArray();
			crl::on_main(guard, [=] {
				GenerateGoodThumbnail(document, bytes);
			});
		} else if (active) {
			crl::async([=] {
				const auto image = App::readImage(value, nullptr, false);
				crl::on_main(guard, [=] {
					document->setGoodThumbnailChecked(true);
					if (const auto active = document->activeMediaView()) {
						active->setGoodThumbnail(image);
					}
				});
			});
		} else {
			crl::on_main(guard, [=] {
				document->setGoodThumbnailChecked(true);
			});
		}
	};
	document->owner().cache().get(document->goodThumbnailCacheKey(), got);
}

} // namespace Data
