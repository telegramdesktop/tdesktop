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

	[[nodiscard]] TimeId date() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool displayLoading() const;
	void cancel();
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int32 loadOffset() const;
	[[nodiscard]] bool uploading() const;
	[[nodiscard]] bool cancelled() const;

	void setWaitingForAlbum();
	[[nodiscard]] bool waitingForAlbum() const;

	[[nodiscard]] Image *getReplyPreview(
		Data::FileOrigin origin,
		not_null<PeerData*> context,
		bool spoiler);
	[[nodiscard]] Image *getReplyPreview(not_null<HistoryItem*> item);
	[[nodiscard]] bool replyPreviewLoaded(bool spoiler) const;

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

	void setFields(TimeId date, bool hasAttachedStickers);
	void setExtendedMediaPreview(
		QSize dimensions,
		const QByteArray &inlineThumbnailBytes,
		std::optional<TimeId> videoDuration);
	[[nodiscard]] bool extendedMediaPreview() const;
	[[nodiscard]] std::optional<TimeId> extendedMediaVideoDuration() const;

	void updateImages(
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &videoSmall,
		const ImageWithLocation &videoLarge,
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
	[[nodiscard]] bool hasVideoSmall() const;
	[[nodiscard]] bool videoLoading(Data::PhotoSize size) const;
	[[nodiscard]] bool videoFailed(Data::PhotoSize size) const;
	void loadVideo(Data::PhotoSize size, Data::FileOrigin origin);
	[[nodiscard]] const ImageLocation &videoLocation(
		Data::PhotoSize size) const;
	[[nodiscard]] int videoByteSize(Data::PhotoSize size) const;
	[[nodiscard]] crl::time videoStartPosition() const;
	void setVideoPlaybackFailed();
	[[nodiscard]] bool videoPlaybackFailed() const;
	[[nodiscard]] bool videoCanBePlayed() const;
	[[nodiscard]] auto createStreamingLoader(
		Data::FileOrigin origin,
		bool forceRemoteLoader) const
	-> std::unique_ptr<Media::Streaming::Loader>;

	[[nodiscard]] bool hasAttachedStickers() const;
	void setHasAttachedStickers(bool value);

	// For now they return size of the 'large' image.
	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;

	PhotoId id = 0;

	PeerData *peer = nullptr; // for chat and channel photos connection
	// geo, caption

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	[[nodiscard]] Data::CloudFile &videoFile(Data::PhotoSize size);
	[[nodiscard]] const Data::CloudFile &videoFile(
		Data::PhotoSize size) const;

	TimeId _dateOrExtendedVideoDuration = 0;

	struct VideoSizes {
		Data::CloudFile small;
		Data::CloudFile large;
		crl::time startTime = 0;
		bool playbackFailed = false;
	};
	QByteArray _inlineThumbnailBytes;
	std::array<Data::CloudFile, Data::kPhotoSizeCount> _images;
	std::unique_ptr<VideoSizes> _videoSizes;

	int32 _dc = 0;
	uint64 _access = 0;
	bool _hasStickers = false;
	bool _extendedMediaPreview = false;

	QByteArray _fileReference;
	std::unique_ptr<Data::ReplyPreview> _replyPreview;
	std::weak_ptr<Data::PhotoMedia> _media;

	not_null<Data::Session*> _owner;

};
