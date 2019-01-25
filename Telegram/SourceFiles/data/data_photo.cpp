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
#include "mainwidget.h"
#include "core/application.h"

PhotoData::PhotoData(not_null<Data::Session*> owner, PhotoId id)
: id(id)
, _owner(owner) {
}

Data::Session &PhotoData::owner() const {
	return *_owner;
}

AuthSession &PhotoData::session() const {
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

MTPInputPhoto PhotoData::mtpInput() const {
	return MTP_inputPhoto(
		MTP_long(id),
		MTP_long(access),
		MTP_bytes(fileReference));
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
			if (const auto location = now->location(); !location.isNull()) {
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

void PhotoOpenClickHandler::onClickImpl() const {
	Core::App().showPhoto(this);
}

void PhotoSaveClickHandler::onClickImpl() const {
	auto data = photo();
	if (!data->date) return;

	data->download(context());
}

void PhotoCancelClickHandler::onClickImpl() const {
	auto data = photo();
	if (!data->date) return;

	if (data->uploading()) {
		if (const auto item = App::histItemById(context())) {
			App::main()->cancelUploadLayer(item);
		}
	} else {
		data->cancel();
	}
}
