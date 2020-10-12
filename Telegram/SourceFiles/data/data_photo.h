/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "data/data_cloud_file.h"

namespace Main {
class Session;
} // namespace Main

namespace Media {
namespace Streaming {
class Loader;
} // namespace Streaming
} // namespace Media

namespace Data {

class Session;
class ReplyPreview;
class PhotoMedia;

inline constexpr auto kPhotoSizeCount = 3;

enum class PhotoSize : uchar {
	Small,
	Thumbnail,
	Large,
};

[[nodiscard]] inline int PhotoSizeIndex(PhotoSize size) {
	Expects(static_cast<int>(size) < kPhotoSizeCount);

	return static_cast<int>(size);
}

} // namespace Data

class PhotoData final {
public:
	PhotoData(not_null<Data::Session*> owner, PhotoId id);
	~PhotoData();

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] bool isNull() const;

	void automaticLoadSettingsChanged();

	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool displayLoading() const;
	void cancel();
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int32 loadOffset() const;
	[[nodiscard]] bool uploading() const;
	[[nodiscard]] bool cancelled() const;

	void setWaitingForAlbum();
	[[nodiscard]] bool waitingForAlbum() const;

	[[nodiscard]] Image *getReplyPreview(Data::FileOrigin origin);
	[[nodiscard]] bool replyPreviewLoaded() const;

	void setRemoteLocation(
		int32 dc,
		uint64 access,
		const QByteArray &fileReference);
	[[nodiscard]] MTPInputPhoto mtpInput() const;
	[[nodiscard]] QByteArray fileReference() const;
	void refreshFileReference(const QByteArray &value);

	// When we have some client-side generated photo
	// (for example for displaying an external inline bot result)
	// and it has downloaded full image, we can collect image from it
	// to (this) received from the server "same" photo.
	void collectLocalData(not_null<PhotoData*> local);

	[[nodiscard]] std::shared_ptr<Data::PhotoMedia> createMediaView();
	[[nodiscard]] auto activeMediaView() const
		-> std::shared_ptr<Data::PhotoMedia>;

	void updateImages(
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &video,
		crl::time videoStartTime);
	[[nodiscard]] int validSizeIndex(Data::PhotoSize size) const;
	[[nodiscard]] int existingSizeIndex(Data::PhotoSize size) const;

	[[nodiscard]] QByteArray inlineThumbnailBytes() const {
		return _inlineThumbnailBytes;
	}
	void clearInlineThumbnailBytes() {
		_inlineThumbnailBytes = QByteArray();
	}

	void load(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal,
		bool autoLoading = false);

	[[nodiscard]] static int SideLimit();

	[[nodiscard]] bool hasExact(Data::PhotoSize size) const;
	[[nodiscard]] bool loading(Data::PhotoSize size) const;
	[[nodiscard]] bool failed(Data::PhotoSize size) const;
	void clearFailed(Data::PhotoSize size);
	void load(
		Data::PhotoSize size,
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal,
		bool autoLoading = false);
	[[nodiscard]] const ImageLocation &location(Data::PhotoSize size) const;
	[[nodiscard]] std::optional<QSize> size(Data::PhotoSize size) const;
	[[nodiscard]] int imageByteSize(Data::PhotoSize size) const;

	[[nodiscard]] bool hasVideo() const;
	[[nodiscard]] bool videoLoading() const;
	[[nodiscard]] bool videoFailed() const;
	void loadVideo(Data::FileOrigin origin);
	[[nodiscard]] const ImageLocation &videoLocation() const;
	[[nodiscard]] int videoByteSize() const;
	[[nodiscard]] crl::time videoStartPosition() const {
		return _videoStartTime;
	}
	void setVideoPlaybackFailed() {
		_videoPlaybackFailed = true;
	}
	[[nodiscard]] bool videoPlaybackFailed() const {
		return _videoPlaybackFailed;
	}
	[[nodiscard]] bool videoCanBePlayed() const;
	[[nodiscard]] auto createStreamingLoader(
		Data::FileOrigin origin,
		bool forceRemoteLoader) const
	-> std::unique_ptr<Media::Streaming::Loader>;

	[[nodiscard]] bool hasAttachedStickers() const;
	void setHasAttachedStickers(bool value);

	// For now they return size of the 'large' image.
	int width() const;
	int height() const;

	PhotoId id = 0;
	TimeId date = 0;

	PeerData *peer = nullptr; // for chat and channel photos connection
	// geo, caption

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	QByteArray _inlineThumbnailBytes;
	std::array<Data::CloudFile, Data::kPhotoSizeCount> _images;
	Data::CloudFile _video;
	crl::time _videoStartTime = 0;
	bool _videoPlaybackFailed = false;

	int32 _dc = 0;
	uint64 _access = 0;
	bool _hasStickers = false;
	QByteArray _fileReference;
	std::unique_ptr<Data::ReplyPreview> _replyPreview;
	std::weak_ptr<Data::PhotoMedia> _media;

	not_null<Data::Session*> _owner;

};

class PhotoClickHandler : public FileClickHandler {
public:
	PhotoClickHandler(
		not_null<PhotoData*> photo,
		FullMsgId context = FullMsgId(),
		PeerData *peer = nullptr);

	[[nodiscard]] not_null<PhotoData*> photo() const {
		return _photo;
	}
	[[nodiscard]] PeerData *peer() const {
		return _peer;
	}

private:
	const not_null<PhotoData*> _photo;
	PeerData * const _peer = nullptr;

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
