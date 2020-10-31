/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_photo.h"

#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_reply_preview.h"
#include "data/data_photo_media.h"
#include "ui/image/image.h"
#include "main/main_session.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_loader_mtproto.h"
#include "mainwidget.h"
#include "storage/file_download.h"
#include "core/application.h"
#include "facades.h"
#include "app.h"

namespace {

constexpr auto kPhotoSideLimit = 1280;

using Data::PhotoMedia;
using Data::PhotoSize;
using Data::PhotoSizeIndex;
using Data::kPhotoSizeCount;

} // namespace

PhotoData::PhotoData(not_null<Data::Session*> owner, PhotoId id)
: id(id)
, _owner(owner) {
}

PhotoData::~PhotoData() {
	for (auto &image : _images) {
		base::take(image.loader).reset();
	}
	base::take(_video.loader).reset();
}

Data::Session &PhotoData::owner() const {
	return *_owner;
}

Main::Session &PhotoData::session() const {
	return _owner->session();
}

void PhotoData::automaticLoadSettingsChanged() {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	if (!(_images[index].flags & Data::CloudFile::Flag::Cancelled)) {
		return;
	}
	_images[index].loader = nullptr;
	_images[index].flags &= ~Data::CloudFile::Flag::Cancelled;
}

void PhotoData::load(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	load(PhotoSize::Large, origin, fromCloud, autoLoading);
}

bool PhotoData::loading() const {
	return loading(PhotoSize::Large);
}

int PhotoData::validSizeIndex(PhotoSize size) const {
	const auto index = PhotoSizeIndex(size);
	for (auto i = index; i != kPhotoSizeCount; ++i) {
		if (_images[i].location.valid()) {
			return i;
		}
	}
	return PhotoSizeIndex(PhotoSize::Large);
}

int PhotoData::existingSizeIndex(PhotoSize size) const {
	const auto index = PhotoSizeIndex(size);
	for (auto i = index; i != kPhotoSizeCount; ++i) {
		if (_images[i].location.valid() || _images[i].progressivePartSize) {
			return i;
		}
	}
	return PhotoSizeIndex(PhotoSize::Large);
}

bool PhotoData::hasExact(PhotoSize size) const {
	return _images[PhotoSizeIndex(size)].location.valid();
}

bool PhotoData::loading(PhotoSize size) const {
	const auto valid = validSizeIndex(size);
	const auto existing = existingSizeIndex(size);
	if (!_images[valid].loader) {
		return false;
	} else if (valid == existing) {
		return true;
	}
	return (_images[valid].loader->loadSize()
		>= _images[existing].progressivePartSize);
}

bool PhotoData::failed(PhotoSize size) const {
	const auto flags = _images[validSizeIndex(size)].flags;
	return (flags & Data::CloudFile::Flag::Failed);
}

void PhotoData::clearFailed(PhotoSize size) {
	_images[validSizeIndex(size)].flags &= ~Data::CloudFile::Flag::Failed;
}

const ImageLocation &PhotoData::location(PhotoSize size) const {
	return _images[validSizeIndex(size)].location;
}

int PhotoData::SideLimit() {
	return kPhotoSideLimit;
}

std::optional<QSize> PhotoData::size(PhotoSize size) const {
	const auto &provided = location(size);
	const auto result = QSize{ provided.width(), provided.height() };
	const auto limit = SideLimit();
	if (result.isEmpty()) {
		return std::nullopt;
	} else if (result.width() <= limit && result.height() <= limit) {
		return result;
	}
	const auto scaled = result.scaled(limit, limit, Qt::KeepAspectRatio);
	return QSize(std::max(scaled.width(), 1), std::max(scaled.height(), 1));
}

int PhotoData::imageByteSize(PhotoSize size) const {
	const auto existing = existingSizeIndex(size);
	if (const auto result = _images[existing].progressivePartSize) {
		return result;
	}
	return _images[validSizeIndex(size)].byteSize;
}

bool PhotoData::displayLoading() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	if (const auto loader = _images[index].loader.get()) {
		return !loader->finished()
			&& (!loader->loadingLocal() || !loader->autoLoading());
	}
	return (uploading() && !waitingForAlbum());
}

void PhotoData::cancel() {
	if (loading()) {
		_images[PhotoSizeIndex(PhotoSize::Large)].loader->cancel();
	}
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			const auto result = float64(uploadingData->offset)
				/ uploadingData->size;
			return snap(result, 0., 1.);
		}
		return 0.;
	}
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return loading() ? _images[index].loader->currentProgress() : 0.;
}

bool PhotoData::cancelled() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return (_images[index].flags & Data::CloudFile::Flag::Cancelled);
}

void PhotoData::setWaitingForAlbum() {
	if (uploading()) {
		uploadingData->waitingForAlbum = true;
	}
}

bool PhotoData::waitingForAlbum() const {
	return uploading() && uploadingData->waitingForAlbum;
}

int32 PhotoData::loadOffset() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return loading() ? _images[index].loader->currentOffset() : 0;
}

bool PhotoData::uploading() const {
	return (uploadingData != nullptr);
}

Image *PhotoData::getReplyPreview(Data::FileOrigin origin) {
	if (!_replyPreview) {
		_replyPreview = std::make_unique<Data::ReplyPreview>(this);
	}
	return _replyPreview->image(origin);
}

bool PhotoData::replyPreviewLoaded() const {
	if (!_replyPreview) {
		return false;
	}
	return _replyPreview->loaded();
}

void PhotoData::setRemoteLocation(
		int32 dc,
		uint64 access,
		const QByteArray &fileReference) {
	_fileReference = fileReference;
	if (_dc != dc || _access != access) {
		_dc = dc;
		_access = access;
	}
}

MTPInputPhoto PhotoData::mtpInput() const {
	return MTP_inputPhoto(
		MTP_long(id),
		MTP_long(_access),
		MTP_bytes(_fileReference));
}

QByteArray PhotoData::fileReference() const {
	return _fileReference;
}

void PhotoData::refreshFileReference(const QByteArray &value) {
	_fileReference = value;
	for (auto &image : _images) {
		image.location.refreshFileReference(value);
	}
}

void PhotoData::collectLocalData(not_null<PhotoData*> local) {
	if (local == this) {
		return;
	}

	for (auto i = 0; i != kPhotoSizeCount; ++i) {
		if (const auto from = local->_images[i].location.file().cacheKey()) {
			if (const auto to = _images[i].location.file().cacheKey()) {
				_owner->cache().copyIfEmpty(from, to);
			}
		}
	}
	if (const auto localMedia = local->activeMediaView()) {
		auto media = createMediaView();
		media->collectLocalData(localMedia.get());
		_owner->keepAlive(std::move(media));
	}
}

bool PhotoData::isNull() const {
	return !_images[PhotoSizeIndex(PhotoSize::Large)].location.valid();
}

void PhotoData::load(
		PhotoSize size,
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	const auto valid = validSizeIndex(size);
	const auto existing = existingSizeIndex(size);

	// Could've changed, if the requested size didn't have a location.
	const auto validSize = static_cast<PhotoSize>(valid);
	const auto finalCheck = [=] {
		if (const auto active = activeMediaView()) {
			return !active->image(size);
		}
		return true;
	};
	const auto done = [=](QImage result) {
		Expects(_images[valid].loader != nullptr);

		// Find out what progressive photo size have we loaded exactly.
		auto goodFor = validSize;
		const auto loadSize = _images[valid].loader->loadSize();
		if (valid > 0 && _images[valid].byteSize > loadSize) {
			for (auto i = valid; i != 0;) {
				--i;
				const auto required = _images[i].progressivePartSize;
				if (required > 0 && required <= loadSize) {
					goodFor = static_cast<PhotoSize>(i);
					break;
				}
			}
		}
		if (const auto active = activeMediaView()) {
			active->set(validSize, goodFor, std::move(result));
		}
		if (validSize == PhotoSize::Large && goodFor == validSize) {
			_owner->photoLoadDone(this);
		}
	};
	const auto fail = [=](bool started) {
		if (validSize == PhotoSize::Large) {
			_owner->photoLoadFail(this, started);
		}
	};
	const auto progress = [=] {
		if (validSize == PhotoSize::Large) {
			_owner->photoLoadProgress(this);
		}
	};
	Data::LoadCloudFile(
		&session(),
		_images[valid],
		origin,
		fromCloud,
		autoLoading,
		Data::kImageCacheTag,
		finalCheck,
		done,
		fail,
		progress,
		_images[existing].progressivePartSize);

	if (size == PhotoSize::Large) {
		_owner->notifyPhotoLayoutChanged(this);
	}
}

std::shared_ptr<PhotoMedia> PhotoData::createMediaView() {
	if (auto result = activeMediaView()) {
		return result;
	}
	auto result = std::make_shared<PhotoMedia>(this);
	_media = result;
	return result;
}

std::shared_ptr<PhotoMedia> PhotoData::activeMediaView() const {
	return _media.lock();
}

void PhotoData::updateImages(
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &video,
		crl::time videoStartTime) {
	if (!inlineThumbnailBytes.isEmpty()
		&& _inlineThumbnailBytes.isEmpty()) {
		_inlineThumbnailBytes = inlineThumbnailBytes;
	}
	const auto update = [&](PhotoSize size, const ImageWithLocation &data) {
		Data::UpdateCloudFile(
			_images[PhotoSizeIndex(size)],
			data,
			owner().cache(),
			Data::kImageCacheTag,
			[=](Data::FileOrigin origin) { load(size, origin); },
			[=](QImage preloaded) {
				if (const auto media = activeMediaView()) {
					media->set(size, size, data.preloaded);
				}
			});
	};
	update(PhotoSize::Small, small);
	update(PhotoSize::Thumbnail, thumbnail);
	update(PhotoSize::Large, large);

	if (video.location.valid()) {
		_videoStartTime = videoStartTime;
	}
	Data::UpdateCloudFile(
		_video,
		video,
		owner().cache(),
		Data::kAnimationCacheTag,
		[&](Data::FileOrigin origin) { loadVideo(origin); });
}

[[nodiscard]] bool PhotoData::hasAttachedStickers() const {
	return _hasStickers;
}

void PhotoData::setHasAttachedStickers(bool value) {
	_hasStickers = value;
}

int PhotoData::width() const {
	return _images[PhotoSizeIndex(PhotoSize::Large)].location.width();
}

int PhotoData::height() const {
	return _images[PhotoSizeIndex(PhotoSize::Large)].location.height();
}

bool PhotoData::hasVideo() const {
	return _video.location.valid();
}

bool PhotoData::videoLoading() const {
	return _video.loader != nullptr;
}

bool PhotoData::videoFailed() const {
	return (_video.flags & Data::CloudFile::Flag::Failed);
}

void PhotoData::loadVideo(Data::FileOrigin origin) {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeMediaView()) {
			return active->videoContent().isEmpty();
		}
		return true;
	};
	const auto done = [=](QByteArray result) {
		if (const auto active = activeMediaView()) {
			active->setVideo(std::move(result));
		}
	};
	Data::LoadCloudFile(
		&session(),
		_video,
		origin,
		LoadFromCloudOrLocal,
		autoLoading,
		Data::kAnimationCacheTag,
		finalCheck,
		done);
}

const ImageLocation &PhotoData::videoLocation() const {
	return _video.location;
}

int PhotoData::videoByteSize() const {
	return _video.byteSize;
}

bool PhotoData::videoCanBePlayed() const {
	return hasVideo() && !videoPlaybackFailed();
}

auto PhotoData::createStreamingLoader(
	Data::FileOrigin origin,
	bool forceRemoteLoader) const
-> std::unique_ptr<Media::Streaming::Loader> {
	if (!hasVideo()) {
		return nullptr;
	}
	if (!forceRemoteLoader) {
		const auto media = activeMediaView();
		if (media && !media->videoContent().isEmpty()) {
			return Media::Streaming::MakeBytesLoader(media->videoContent());
		}
	}
	return v::is<StorageFileLocation>(videoLocation().file().data)
		? std::make_unique<Media::Streaming::LoaderMtproto>(
			&session().downloader(),
			v::get<StorageFileLocation>(videoLocation().file().data),
			videoByteSize(),
			origin)
		: nullptr;
}

PhotoClickHandler::PhotoClickHandler(
	not_null<PhotoData*> photo,
	FullMsgId context,
	PeerData *peer)
: FileClickHandler(&photo->session(), context)
, _photo(photo)
, _peer(peer) {
}

void PhotoOpenClickHandler::onClickImpl() const {
	Core::App().showPhoto(this);
}

void PhotoSaveClickHandler::onClickImpl() const {
	const auto data = photo();
	if (!data->date) {
		return;
	} else {
		data->clearFailed(PhotoSize::Large);
		data->load(context());
	}
}

void PhotoCancelClickHandler::onClickImpl() const {
	const auto data = photo();
	if (!data->date) {
		return;
	} else if (data->uploading()) {
		if (const auto item = data->owner().message(context())) {
			if (const auto m = App::main()) { // multi good
				if (&m->session() == &data->session()) {
					m->cancelUploadLayer(item);
				}
			}
		}
	} else {
		data->cancel();
	}
}
