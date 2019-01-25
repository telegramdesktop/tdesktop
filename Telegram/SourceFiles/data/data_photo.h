/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class AuthSession;

namespace Data {
class Session;
} // namespace Data

class PhotoData {
public:
	PhotoData(not_null<Data::Session*> owner, PhotoId id);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] AuthSession &session() const;

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item);
	void automaticLoadSettingsChanged();

	void download(Data::FileOrigin origin);
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool displayLoading() const;
	void cancel();
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int32 loadOffset() const;
	[[nodiscard]] bool uploading() const;

	void setWaitingForAlbum();
	[[nodiscard]] bool waitingForAlbum() const;

	void unload();
	[[nodiscard]] Image *getReplyPreview(Data::FileOrigin origin);

	[[nodiscard]] MTPInputPhoto mtpInput() const;

	// When we have some client-side generated photo
	// (for example for displaying an external inline bot result)
	// and it has downloaded full image, we can collect image from it
	// to (this) received from the server "same" photo.
	void collectLocalData(not_null<PhotoData*> local);

	bool isNull() const;

	void loadThumbnail(Data::FileOrigin origin);
	void loadThumbnailSmall(Data::FileOrigin origin);
	Image *thumbnailInline() const;
	not_null<Image*> thumbnailSmall() const;
	not_null<Image*> thumbnail() const;

	void load(Data::FileOrigin origin);
	not_null<Image*> large() const;

	// For now they return size of the 'large' image.
	int width() const;
	int height() const;

	void updateImages(
		ImagePtr thumbnailInline,
		ImagePtr thumbnailSmall,
		ImagePtr thumbnail,
		ImagePtr large);

	PhotoId id = 0;
	uint64 access = 0;
	QByteArray fileReference;
	TimeId date = 0;

	PeerData *peer = nullptr; // for chat and channel photos connection
	// geo, caption

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	ImagePtr _thumbnailInline;
	ImagePtr _thumbnailSmall;
	ImagePtr _thumbnail;
	ImagePtr _large;

	Data::ReplyPreview _replyPreview;

	not_null<Data::Session*> _owner;

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
