/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document_media.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_file_origin.h"
#include "data/data_auto_download.h"
#include "media/clip/media_clip_reader.h"
#include "main/main_session.h"
#include "lottie/lottie_animation.h"
#include "history/history_item.h"
#include "history/history.h"
#include "window/themes/window_theme_preview.h"
#include "storage/file_download.h"
#include "ui/image/image.h"
#include "facades.h"
#include "app.h"

#include <QtCore/QBuffer>
#include <QtGui/QImageReader>

namespace Data {
namespace {

constexpr auto kReadAreaLimit = 12'032 * 9'024;
constexpr auto kWallPaperThumbnailLimit = 960;
constexpr auto kMaxVideoFrameArea = 7'680 * 4'320;
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

DocumentMedia::DocumentMedia(not_null<DocumentData*> owner)
: _owner(owner) {
}

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
	_goodThumbnail = std::make_unique<Image>(
		std::make_unique<Images::ImageSource>(std::move(thumbnail), "PNG"));
	_owner->session().downloaderTaskFinished().notify();
}

Image *DocumentMedia::thumbnailInline() const {
	if (!_inlineThumbnail) {
		auto image = Images::FromInlineBytes(_owner->inlineThumbnailBytes());
		if (!image.isNull()) {
			_inlineThumbnail = std::make_unique<Image>(
				std::make_unique<Images::ImageSource>(
					std::move(image),
					"PNG"));
		}
	}
	return _inlineThumbnail.get();
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
			_sticker = std::make_unique<Image>(
				std::make_unique<Images::LocalFileSource>(loc.name()));
			loc.accessDisable();
		}
	} else {
		auto format = QByteArray();
		auto image = App::readImage(_bytes, &format, false);
		_sticker = std::make_unique<Image>(
			std::make_unique<Images::LocalFileSource>(
				QString(),
				_bytes,
				format,
				std::move(image)));
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
	if (!toCache && Global::AskDownloadPath()) {
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

void DocumentMedia::checkStickerSmall() {
	const auto data = _owner->sticker();
	if ((data && data->animated) || _owner->thumbnailEnoughForSticker()) {
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
	if ((data && data->animated) || _owner->thumbnailEnoughForSticker()) {
		return _owner->thumbnail();
	}
	return _sticker.get();
}

void DocumentMedia::checkStickerLarge(not_null<FileLoader*> loader) {
	if (_owner->sticker()
		&& !_sticker
		&& !loader->imageData().isNull()
		&& !_bytes.isEmpty()) {
		_sticker = std::make_unique<Image>(
			std::make_unique<Images::LocalFileSource>(
				QString(),
				_bytes,
				loader->imageFormat(),
				loader->imageData()));
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
		: std::make_unique<FileLocation>(document->location());
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
