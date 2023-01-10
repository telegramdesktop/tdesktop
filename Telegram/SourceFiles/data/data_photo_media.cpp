/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_photo_media.h"

#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_auto_download.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "history/history_item.h"
#include "history/history.h"
#include "storage/file_download.h"
#include "ui/image/image.h"

namespace Data {

PhotoMedia::PhotoMedia(not_null<PhotoData*> owner)
: _owner(owner) {
}

// NB! Right now DocumentMedia can outlive Main::Session!
// In DocumentData::collectLocalData a shared_ptr is sent on_main.
// In case this is a problem the ~Gif code should be rewritten.
PhotoMedia::~PhotoMedia() = default;

not_null<PhotoData*> PhotoMedia::owner() const {
	return _owner;
}

Image *PhotoMedia::thumbnailInline() const {
	if (!_inlineThumbnail) {
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

Image *PhotoMedia::image(PhotoSize size) const {
	if (const auto resolved = resolveLoadedImage(size)) {
		return resolved->data.get();
	}
	return nullptr;
}

QByteArray PhotoMedia::imageBytes(PhotoSize size) const {
	if (const auto resolved = resolveLoadedImage(size)) {
		return resolved->bytes;
	}
	return QByteArray();
}

auto PhotoMedia::resolveLoadedImage(PhotoSize size) const
-> const PhotoImage * {
	const auto &original = _images[PhotoSizeIndex(size)];
	if (const auto image = original.data.get()) {
		if (original.goodFor >= size) {
			return &original;
		}
	}
	const auto &valid = _images[_owner->validSizeIndex(size)];
	if (const auto image = valid.data.get()) {
		if (valid.goodFor >= size) {
			return &valid;
		}
	}
	return nullptr;
}

void PhotoMedia::wanted(PhotoSize size, Data::FileOrigin origin) {
	const auto index = _owner->validSizeIndex(size);
	if (!_images[index].data || _images[index].goodFor < size) {
		_owner->load(size, origin);
	}
}

QSize PhotoMedia::size(PhotoSize size) const {
	const auto index = PhotoSizeIndex(size);
	if (const auto image = _images[index].data.get()) {
		return image->size();
	}
	const auto &location = _owner->location(size);
	return { location.width(), location.height() };
}

void PhotoMedia::set(
		PhotoSize size,
		PhotoSize goodFor,
		QImage image,
		QByteArray bytes) {
	const auto index = PhotoSizeIndex(size);
	const auto limit = PhotoData::SideLimit();
	if (image.width() > limit || image.height() > limit) {
		image = image.scaled(
			limit,
			limit,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	_images[index] = PhotoImage{
		.data = std::make_unique<Image>(std::move(image)),
		.bytes = std::move(bytes),
		.goodFor = goodFor,
	};
	_owner->session().notifyDownloaderTaskFinished();
}

QByteArray PhotoMedia::videoContent(PhotoSize size) const {
	const auto small = (size == PhotoSize::Small) && _owner->hasVideoSmall();
	return small ? _videoBytesSmall : _videoBytesLarge;
}

QSize PhotoMedia::videoSize(PhotoSize size) const {
	const auto &location = _owner->videoLocation(size);
	return { location.width(), location.height() };
}

void PhotoMedia::videoWanted(PhotoSize size, Data::FileOrigin origin) {
	if (videoContent(size).isEmpty()) {
		_owner->loadVideo(size, origin);
	}
}

void PhotoMedia::setVideo(PhotoSize size, QByteArray content) {
	const auto small = (size == PhotoSize::Small) && _owner->hasVideoSmall();
	(small ? _videoBytesSmall : _videoBytesLarge) = std::move(content);
}

bool PhotoMedia::loaded() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return (_images[index].data != nullptr)
		&& (_images[index].goodFor >= PhotoSize::Large);
}

float64 PhotoMedia::progress() const {
	return (_owner->uploading() || _owner->loading())
		? _owner->progress()
		: (loaded() ? 1. : 0.);
}

bool PhotoMedia::autoLoadThumbnailAllowed(not_null<PeerData*> peer) const {
	if (loaded() || _owner->cancelled()) {
		return false;
	}
	return _owner->hasExact(PhotoSize::Small)
		|| _owner->hasExact(PhotoSize::Thumbnail)
		|| AutoDownload::Should(
			_owner->session().settings().autoDownload(),
			peer,
			_owner);
}

void PhotoMedia::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (!item || loaded() || _owner->cancelled()) {
		return;
	}
	const auto loadFromCloud = Data::AutoDownload::Should(
		_owner->session().settings().autoDownload(),
		item->history()->peer,
		_owner);
	_owner->load(
		origin,
		loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly,
		true);
}

void PhotoMedia::collectLocalData(not_null<PhotoMedia*> local) {
	if (const auto image = local->_inlineThumbnail.get()) {
		_inlineThumbnail = std::make_unique<Image>(image->original());
	}
	for (auto i = 0; i != kPhotoSizeCount; ++i) {
		if (const auto image = local->_images[i].data.get()) {
			_images[i] = PhotoImage{
				.data = std::make_unique<Image>(image->original()),
				.goodFor = local->_images[i].goodFor
			};
		}
	}
}

bool PhotoMedia::saveToFile(const QString &path) {
	constexpr auto large = PhotoSize::Large;
	if (const auto video = videoContent(large); !video.isEmpty()) {
		QFile f(path);
		return f.open(QIODevice::WriteOnly)
			&& (f.write(video) == video.size());
	} else if (const auto photo = imageBytes(large)
		; !photo.isEmpty()) {
		QFile f(path);
		return f.open(QIODevice::WriteOnly)
			&& (f.write(photo) == photo.size());
	} else if (const auto fallback = image(large)->original()
		; !fallback.isNull()) {
		return fallback.save(path, "JPG");
	}
	return false;
}

} // namespace Data
