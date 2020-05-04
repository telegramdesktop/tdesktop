/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_photo.h"

#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "core/application.h"
#include "facades.h"
#include "app.h"

PhotoData::PhotoData(not_null<Data::Session*> owner, PhotoId id)
: id(id)
, _owner(owner) {
}

Data::Session &PhotoData::owner() const {
	return *_owner;
}

Main::Session &PhotoData::session() const {
	return _owner->session();
}

void PhotoData::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	_large->automaticLoad(origin, item);
}

void PhotoData::automaticLoadSettingsChanged() {
	_large->automaticLoadSettingsChanged();
}

void PhotoData::download(Data::FileOrigin origin) {
	_large->loadEvenCancelled(origin);
	_owner->notifyPhotoLayoutChanged(this);
}

bool PhotoData::loaded() const {
	bool wasLoading = loading();
	if (_large->loaded()) {
		if (wasLoading) {
			_owner->notifyPhotoLayoutChanged(this);
		}
		return true;
	}
	return false;
}

bool PhotoData::loading() const {
	return _large->loading();
}

bool PhotoData::displayLoading() const {
	return _large->loading()
		? _large->displayLoading()
		: (uploading() && !waitingForAlbum());
}

void PhotoData::cancel() {
	_large->cancel();
	_owner->notifyPhotoLayoutChanged(this);
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			return float64(uploadingData->offset) / uploadingData->size;
		}
		return 0;
	}
	return _large->progress();
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
	return _large->loadOffset();
}

bool PhotoData::uploading() const {
	return (uploadingData != nullptr);
}

void PhotoData::unload() {
	// Forget thumbnail only when image cache limit exceeds.
	//_thumbnailInline->unload();
	_thumbnailSmall->unload();
	_thumbnail->unload();
	_large->unload();
	_replyPreview.clear();
}

Image *PhotoData::getReplyPreview(Data::FileOrigin origin) {
	if (_replyPreview
		&& (_replyPreview.good() || !_thumbnailSmall->loaded())) {
		return _replyPreview.image();
	}
	if (_thumbnailSmall->isDelayedStorageImage()
		&& !_large->isNull()
		&& !_large->isDelayedStorageImage()
		&& _large->loaded()) {
		_replyPreview.prepare(
			_large.get(),
			origin,
			Images::Option(0));
	} else if (_thumbnailSmall->loaded()) {
		_replyPreview.prepare(
			_thumbnailSmall.get(),
			origin,
			Images::Option(0));
	} else {
		_thumbnailSmall->load(origin);
		if (_thumbnailInline) {
			_replyPreview.prepare(
				_thumbnailInline.get(),
				origin,
				Images::Option::Blurred);
		}
	}
	return _replyPreview.image();
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
	_thumbnailSmall->refreshFileReference(value);
	_thumbnail->refreshFileReference(value);
	_large->refreshFileReference(value);
}

void PhotoData::collectLocalData(not_null<PhotoData*> local) {
	if (local == this) {
		return;
	}

	const auto copyImage = [&](const ImagePtr &src, const ImagePtr &dst) {
		if (const auto from = src->cacheKey()) {
			if (const auto to = dst->cacheKey()) {
				_owner->cache().copyIfEmpty(*from, *to);
			}
		}
	};
	copyImage(local->_thumbnailSmall, _thumbnailSmall);
	copyImage(local->_thumbnail, _thumbnail);
	copyImage(local->_large, _large);
}

bool PhotoData::isNull() const {
	return _large->isNull();
}

void PhotoData::loadThumbnail(Data::FileOrigin origin) {
	_thumbnail->load(origin);
}

void PhotoData::loadThumbnailSmall(Data::FileOrigin origin) {
	_thumbnailSmall->load(origin);
}

Image *PhotoData::thumbnailInline() const {
	return _thumbnailInline ? _thumbnailInline.get() : nullptr;
}

not_null<Image*> PhotoData::thumbnailSmall() const {
	return _thumbnailSmall.get();
}

not_null<Image*> PhotoData::thumbnail() const {
	return _thumbnail.get();
}

void PhotoData::load(Data::FileOrigin origin) {
	_large->load(origin);
}

not_null<Image*> PhotoData::large() const {
	return _large.get();
}

void PhotoData::updateImages(
		ImagePtr thumbnailInline,
		ImagePtr thumbnailSmall,
		ImagePtr thumbnail,
		ImagePtr large) {
	if (!thumbnailSmall || !thumbnail || !large) {
		return;
	}
	if (thumbnailInline && !_thumbnailInline) {
		_thumbnailInline = thumbnailInline;
	}
	const auto update = [](ImagePtr &was, ImagePtr now) {
		if (!was) {
			was = now;
		} else if (was->isDelayedStorageImage()) {
			if (const auto location = now->location(); location.valid()) {
				was->setDelayedStorageLocation(
					Data::FileOrigin(),
					location);
			}
		}
	};
	update(_thumbnailSmall, thumbnailSmall);
	update(_thumbnail, thumbnail);
	update(_large, large);
}

int PhotoData::width() const {
	return _large->width();
}

int PhotoData::height() const {
	return _large->height();
}

PhotoClickHandler::PhotoClickHandler(
	not_null<PhotoData*> photo,
	FullMsgId context,
	PeerData *peer)
: FileClickHandler(context)
, _session(&photo->session())
, _photo(photo)
, _peer(peer) {
}

void PhotoOpenClickHandler::onClickImpl() const {
	if (valid()) {
		Core::App().showPhoto(this);
	}
}

void PhotoSaveClickHandler::onClickImpl() const {
	if (!valid()) {
		return;
	}
	const auto data = photo();
	if (!data->date) {
		return;
	} else {
		data->download(context());
	}
}

void PhotoCancelClickHandler::onClickImpl() const {
	if (!valid()) {
		return;
	}
	const auto data = photo();
	if (!data->date) {
		return;
	} else if (data->uploading()) {
		if (const auto item = data->owner().message(context())) {
			App::main()->cancelUploadLayer(item);
		}
	} else {
		data->cancel();
	}
}
