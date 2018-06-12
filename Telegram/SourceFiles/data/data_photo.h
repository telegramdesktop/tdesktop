/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class PhotoData {
public:
	PhotoData(
		const PhotoId &id,
		const uint64 &access = 0,
		int32 date = 0,
		const ImagePtr &thumb = ImagePtr(),
		const ImagePtr &medium = ImagePtr(),
		const ImagePtr &full = ImagePtr());

	void automaticLoad(const HistoryItem *item);
	void automaticLoadSettingsChanged();

	void download();
	bool loaded() const;
	bool loading() const;
	bool displayLoading() const;
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	void setWaitingForAlbum();
	bool waitingForAlbum() const;

	void forget();
	ImagePtr makeReplyPreview();

	PhotoId id;
	uint64 access;
	int32 date;
	ImagePtr thumb, replyPreview;
	ImagePtr medium;
	ImagePtr full;

	PeerData *peer = nullptr; // for chat and channel photos connection
	// geo, caption

	std::unique_ptr<Data::UploadState> uploadingData;

};

class PhotoClickHandler : public FileClickHandler {
public:
	PhotoClickHandler(
		not_null<PhotoData*> photo,
		FullMsgId context = FullMsgId(),
		PeerData *peer = nullptr)
	: FileClickHandler(context)
	, _photo(photo)
	, _peer(peer) {
	}
	not_null<PhotoData*> photo() const {
		return _photo;
	}
	PeerData *peer() const {
		return _peer;
	}

private:
	not_null<PhotoData*> _photo;
	PeerData *_peer = nullptr;

};

class PhotoOpenClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;

protected:
	void onClickImpl() const override;

};

class PhotoSaveClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;

protected:
	void onClickImpl() const override;

};

class PhotoCancelClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;

protected:
	void onClickImpl() const override;

};
