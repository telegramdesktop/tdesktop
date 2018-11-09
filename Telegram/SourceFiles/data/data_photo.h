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
	explicit PhotoData(const PhotoId &id);
	PhotoData(
		const PhotoId &id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const ImagePtr &thumb,
		const ImagePtr &medium,
		const ImagePtr &full);

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item);
	void automaticLoadSettingsChanged();

	void download(Data::FileOrigin origin);
	bool loaded() const;
	bool loading() const;
	bool displayLoading() const;
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	void setWaitingForAlbum();
	bool waitingForAlbum() const;

	void unload();
	Image *getReplyPreview(Data::FileOrigin origin);

	MTPInputPhoto mtpInput() const;

	// When we have some client-side generated photo
	// (for example for displaying an external inline bot result)
	// and it has downloaded full image, we can collect image from it
	// to (this) received from the server "same" photo.
	void collectLocalData(PhotoData *local);

	PhotoId id = 0;
	uint64 access = 0;
	QByteArray fileReference;
	TimeId date = 0;
	ImagePtr thumb;
	ImagePtr medium;
	ImagePtr full;

	PeerData *peer = nullptr; // for chat and channel photos connection
	// geo, caption

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	std::unique_ptr<Image> _replyPreview;

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
