/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document_media.h"

#include "data/data_document.h"
#include "data/data_document_good_thumbnail.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "media/clip/media_clip_reader.h"
#include "main/main_session.h"
#include "lottie/lottie_animation.h"
#include "window/themes/window_theme_preview.h"
#include "ui/image/image.h"
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

void DocumentMedia::GenerateGoodThumbnail(not_null<DocumentData*> document) {
	const auto data = document->data();
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
			crl::on_main(guard, [=] {
				GenerateGoodThumbnail(document);
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
