/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_photo.h"

#include "data/data_session.h"
#include "mainwidget.h"
#include "history/history_media_types.h"
#include "auth_session.h"
#include "messenger.h"

PhotoData::PhotoData(const PhotoId &id)
: id(id) {
}

PhotoData::PhotoData(
	const PhotoId &id,
	const uint64 &access,
	const QByteArray &fileReference,
	TimeId date,
	const ImagePtr &thumb,
	const ImagePtr &medium,
	const ImagePtr &full)
: id(id)
, access(access)
, date(date)
, thumb(thumb)
, medium(medium)
, full(full) {
}

void PhotoData::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	full->automaticLoad(origin, item);
}

void PhotoData::automaticLoadSettingsChanged() {
	full->automaticLoadSettingsChanged();
}

void PhotoData::download(Data::FileOrigin origin) {
	full->loadEvenCancelled(origin);
	Auth().data().notifyPhotoLayoutChanged(this);
}

bool PhotoData::loaded() const {
	bool wasLoading = loading();
	if (full->loaded()) {
		if (wasLoading) {
			Auth().data().notifyPhotoLayoutChanged(this);
		}
		return true;
	}
	return false;
}

bool PhotoData::loading() const {
	return full->loading();
}

bool PhotoData::displayLoading() const {
	return full->loading()
		? full->displayLoading()
		: (uploading() && !waitingForAlbum());
}

void PhotoData::cancel() {
	full->cancel();
	Auth().data().notifyPhotoLayoutChanged(this);
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			return float64(uploadingData->offset) / uploadingData->size;
		}
		return 0;
	}
	return full->progress();
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
	return full->loadOffset();
}

bool PhotoData::uploading() const {
	return (uploadingData != nullptr);
}

void PhotoData::forget() {
	thumb->forget();
	replyPreview->forget();
	medium->forget();
	full->forget();
}

ImagePtr PhotoData::makeReplyPreview(Data::FileOrigin origin) {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			replyPreview = ImagePtr(w > h ? thumb->pix(origin, w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(origin, st::msgReplyBarSize.height()), "PNG");
		} else {
			thumb->load(origin);
		}
	}
	return replyPreview;
}

MTPInputPhoto PhotoData::mtpInput() const {
	return MTP_inputPhoto(
		MTP_long(id),
		MTP_long(access),
		MTP_bytes(fileReference));
}

void PhotoOpenClickHandler::onClickImpl() const {
	Messenger::Instance().showPhoto(this);
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
