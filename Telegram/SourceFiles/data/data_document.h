/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/binary_guard.h"
#include "data/data_types.h"
#include "data/data_cloud_file.h"
#include "core/file_location.h"

enum class ChatRestriction;
class mtpFileLoader;

namespace Images {
class Source;
} // namespace Images

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Media {
namespace Streaming {
class Loader;
} // namespace Streaming
} // namespace Media

namespace Data {
class Session;
class DocumentMedia;
class ReplyPreview;
enum class StickersType : uchar;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

inline uint64 mediaMix32To64(int32 a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32)
		| uint64(*reinterpret_cast<uint32*>(&b));
}

// version field removed from document.
inline MediaKey mediaKey(LocationType type, int32 dc, const uint64 &id) {
	return MediaKey(mediaMix32To64(type, dc), id);
}

struct DocumentAdditionalData {
	virtual ~DocumentAdditionalData() = default;

};

enum class StickerType : uchar {
	Webp,
	Tgs,
	Webm,
};

struct StickerData : public DocumentAdditionalData {
	[[nodiscard]] Data::FileOrigin setOrigin() const;
	[[nodiscard]] bool isStatic() const;
	[[nodiscard]] bool isLottie() const;
	[[nodiscard]] bool isAnimated() const;
	[[nodiscard]] bool isWebm() const;

	QString alt;
	StickerSetIdentifier set;
	StickerType type = StickerType::Webp;
	Data::StickersType setType = Data::StickersType();
};

struct SongData : public DocumentAdditionalData {
	QString title, performer;
};

struct VoiceData : public DocumentAdditionalData {
	~VoiceData();

	VoiceWaveform waveform;
	char wavemax = 0;
};

using RoundData = VoiceData;

namespace Serialize {
class Document;
} // namespace Serialize;

class DocumentData final {
public:
	DocumentData(not_null<Data::Session*> owner, DocumentId id);
	~DocumentData();

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	void setattributes(
		const QVector<MTPDocumentAttribute> &attributes);

	void automaticLoadSettingsChanged();

	[[nodiscard]] bool loading() const;
	[[nodiscard]] QString loadingFilePath() const;
	[[nodiscard]] bool displayLoading() const;
	void save(
		Data::FileOrigin origin,
		const QString &toFile,
		LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal,
		bool autoLoading = false);
	void cancel();
	[[nodiscard]] bool cancelled() const;
	void resetCancelled();
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int64 loadOffset() const;
	[[nodiscard]] bool uploading() const;
	[[nodiscard]] bool loadedInMediaCache() const;
	void setLoadedInMediaCache(bool loaded);

	[[nodiscard]] ChatRestriction requiredSendRight() const;

	void setWaitingForAlbum();
	[[nodiscard]] bool waitingForAlbum() const;

	[[nodiscard]] const Core::FileLocation &location(
		bool check = false) const;
	void setLocation(const Core::FileLocation &loc);

	bool saveFromData();
	bool saveFromDataSilent();
	[[nodiscard]] QString filepath(bool check = false) const;

	void forceToCache(bool force);
	[[nodiscard]] bool saveToCache() const;

	[[nodiscard]] Image *getReplyPreview(
		Data::FileOrigin origin,
		not_null<PeerData*> context,
		bool spoiler);
	[[nodiscard]] Image *getReplyPreview(not_null<HistoryItem*> item);
	[[nodiscard]] bool replyPreviewLoaded(bool spoiler) const;

	[[nodiscard]] StickerData *sticker() const;
	[[nodiscard]] Data::FileOrigin stickerSetOrigin() const;
	[[nodiscard]] Data::FileOrigin stickerOrGifOrigin() const;
	[[nodiscard]] bool isStickerSetInstalled() const;
	[[nodiscard]] SongData *song();
	[[nodiscard]] const SongData *song() const;
	[[nodiscard]] VoiceData *voice();
	[[nodiscard]] const VoiceData *voice() const;
	[[nodiscard]] RoundData *round();
	[[nodiscard]] const RoundData *round() const;

	void forceIsStreamedAnimation();
	[[nodiscard]] bool isVoiceMessage() const;
	[[nodiscard]] bool isVideoMessage() const;
	[[nodiscard]] bool isSong() const;
	[[nodiscard]] bool isSongWithCover() const;
	[[nodiscard]] bool isAudioFile() const;
	[[nodiscard]] bool isVideoFile() const;
	[[nodiscard]] bool isSilentVideo() const;
	[[nodiscard]] bool isAnimation() const;
	[[nodiscard]] bool isGifv() const;
	[[nodiscard]] bool isTheme() const;
	[[nodiscard]] bool isSharedMediaMusic() const;
	[[nodiscard]] crl::time duration() const;
	[[nodiscard]] bool hasDuration() const;
	[[nodiscard]] bool isImage() const;
	void recountIsImage();
	[[nodiscard]] bool supportsStreaming() const;
	void setNotSupportsStreaming();
	void setDataAndCache(const QByteArray &data);
	bool checkWallPaperProperties();
	[[nodiscard]] bool isWallPaper() const;
	[[nodiscard]] bool isPatternWallPaper() const;
	[[nodiscard]] bool isPatternWallPaperPNG() const;
	[[nodiscard]] bool isPatternWallPaperSVG() const;
	[[nodiscard]] bool isPremiumSticker() const;
	[[nodiscard]] bool isPremiumEmoji() const;
	[[nodiscard]] bool emojiUsesTextColor() const;

	[[nodiscard]] bool hasThumbnail() const;
	[[nodiscard]] bool thumbnailLoading() const;
	[[nodiscard]] bool thumbnailFailed() const;
	void loadThumbnail(Data::FileOrigin origin);
	[[nodiscard]] const ImageLocation &thumbnailLocation() const;
	[[nodiscard]] int thumbnailByteSize() const;

	[[nodiscard]] bool hasVideoThumbnail() const;
	[[nodiscard]] bool videoThumbnailLoading() const;
	[[nodiscard]] bool videoThumbnailFailed() const;
	void loadVideoThumbnail(Data::FileOrigin origin);
	[[nodiscard]] const ImageLocation &videoThumbnailLocation() const;
	[[nodiscard]] int videoThumbnailByteSize() const;

	void updateThumbnails(
		const InlineImageLocation &inlineThumbnail,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		bool isPremiumSticker);

	[[nodiscard]] QByteArray inlineThumbnailBytes() const {
		return _inlineThumbnailBytes;
	}
	[[nodiscard]] bool inlineThumbnailIsPath() const {
		return (_flags & Flag::InlineThumbnailIsPath);
	}
	void clearInlineThumbnailBytes() {
		_inlineThumbnailBytes = QByteArray();
	}

	[[nodiscard]] Storage::Cache::Key goodThumbnailCacheKey() const;
	[[nodiscard]] bool goodThumbnailChecked() const;
	[[nodiscard]] bool goodThumbnailGenerating() const;
	[[nodiscard]] bool goodThumbnailNoData() const;
	void setGoodThumbnailGenerating();
	void setGoodThumbnailDataReady();
	void setGoodThumbnailChecked(bool hasData);

	[[nodiscard]] std::shared_ptr<Data::DocumentMedia> createMediaView();
	[[nodiscard]] auto activeMediaView() const
		-> std::shared_ptr<Data::DocumentMedia>;
	void setGoodThumbnailPhoto(not_null<PhotoData*> photo);
	[[nodiscard]] PhotoData *goodThumbnailPhoto() const;

	[[nodiscard]] Storage::Cache::Key bigFileBaseCacheKey() const;

	void setStoryMedia(bool value);
	[[nodiscard]] bool storyMedia() const;

	void setRemoteLocation(
		int32 dc,
		uint64 access,
		const QByteArray &fileReference);
	void setContentUrl(const QString &url);
	void setWebLocation(const WebFileLocation &location);
	[[nodiscard]] bool hasRemoteLocation() const;
	[[nodiscard]] bool hasWebLocation() const;
	[[nodiscard]] bool isNull() const;
	[[nodiscard]] MTPInputDocument mtpInput() const;
	[[nodiscard]] QByteArray fileReference() const;
	void refreshFileReference(const QByteArray &value);

	// When we have some client-side generated document
	// (for example for displaying an external inline bot result)
	// and it has downloaded data, we can collect that data from it
	// to (this) received from the server "same" document.
	void collectLocalData(not_null<DocumentData*> local);

	[[nodiscard]] QString filename() const;
	[[nodiscard]] QString mimeString() const;
	[[nodiscard]] bool hasMimeType(const QString &mime) const;
	void setMimeString(const QString &mime);

	[[nodiscard]] bool hasAttachedStickers() const;

	[[nodiscard]] MediaKey mediaKey() const;
	[[nodiscard]] Storage::Cache::Key cacheKey() const;
	[[nodiscard]] uint8 cacheTag() const;

	[[nodiscard]] bool canBeStreamed(HistoryItem *item) const;
	[[nodiscard]] auto createStreamingLoader(
		Data::FileOrigin origin,
		bool forceRemoteLoader) const
	-> std::unique_ptr<Media::Streaming::Loader>;
	[[nodiscard]] bool useStreamingLoader() const;

	void setInappPlaybackFailed();
	[[nodiscard]] bool inappPlaybackFailed() const;
	[[nodiscard]] int videoPreloadPrefix() const;
	[[nodiscard]] StorageFileLocation videoPreloadLocation() const;

	DocumentId id = 0;
	int64 size = 0;
	QSize dimensions;
	int32 date = 0;
	DocumentType type = FileDocument;
	FileStatus status = FileReady;

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	enum class Flag : ushort {
		StreamingMaybeYes = 0x0001,
		StreamingMaybeNo = 0x0002,
		StreamingPlaybackFailed = 0x0004,
		ImageType = 0x0008,
		DownloadCancelled = 0x0010,
		LoadedInMediaCache = 0x0020,
		HasAttachedStickers = 0x0040,
		InlineThumbnailIsPath = 0x0080,
		ForceToCache = 0x0100,
		PremiumSticker = 0x0200,
		PossibleCoverThumbnail = 0x0400,
		UseTextColor = 0x0800,
		StoryDocument = 0x1000,
		SilentVideo = 0x2000,
	};
	using Flags = base::flags<Flag>;
	friend constexpr bool is_flag_type(Flag) { return true; };

	enum class GoodThumbnailFlag : uchar {
		Checked = 0x01,
		Generating = 0x02,
		NoData = 0x03,
		Mask = 0x03,

		DataReady = 0x04,
	};
	using GoodThumbnailState = base::flags<GoodThumbnailFlag>;
	friend constexpr bool is_flag_type(GoodThumbnailFlag) { return true; };

	static constexpr Flags kStreamingSupportedMask = Flags()
		| Flag::StreamingMaybeYes
		| Flag::StreamingMaybeNo;
	static constexpr Flags kStreamingSupportedUnknown = Flags()
		| Flag::StreamingMaybeYes
		| Flag::StreamingMaybeNo;
	static constexpr Flags kStreamingSupportedMaybeYes = Flags()
		| Flag::StreamingMaybeYes;
	static constexpr Flags kStreamingSupportedMaybeNo = Flags()
		| Flag::StreamingMaybeNo;
	static constexpr Flags kStreamingSupportedNo = Flags();

	friend class Serialize::Document;

	[[nodiscard]] LocationType locationType() const;
	void validateLottieSticker();
	void setMaybeSupportsStreaming(bool supports);
	void setLoadedInMediaCacheLocation();
	void setFileName(const QString &remoteFileName);

	void finishLoad();
	void handleLoaderUpdates();
	void destroyLoader();

	bool saveFromDataChecked();

	void refreshPossibleCoverThumbnail();

	const not_null<Data::Session*> _owner;

	int _videoPreloadPrefix = 0;
	// Two types of location: from MTProto by dc+access or from web by url
	int32 _dc = 0;
	uint64 _access = 0;
	QByteArray _fileReference;
	QString _url;
	QString _filename;
	QString _mimeString;
	WebFileLocation _urlLocation;

	QByteArray _inlineThumbnailBytes;
	Data::CloudFile _thumbnail;
	Data::CloudFile _videoThumbnail;
	std::unique_ptr<Data::ReplyPreview> _replyPreview;
	std::weak_ptr<Data::DocumentMedia> _media;
	PhotoData *_goodThumbnailPhoto = nullptr;
	crl::time _duration = -1;

	Core::FileLocation _location;
	std::unique_ptr<DocumentAdditionalData> _additional;
	mutable Flags _flags = kStreamingSupportedUnknown;
	GoodThumbnailState _goodThumbnailState = GoodThumbnailState();
	std::unique_ptr<FileLoader> _loader;

};

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit);
QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform);

QString FileNameForSave(
	not_null<Main::Session*> session,
	const QString &title,
	const QString &filter,
	const QString &prefix,
	QString name,
	bool savingAs,
	const QDir &dir = QDir());

QString DocumentFileNameForSave(
	not_null<const DocumentData*> data,
	bool forceSavingAs = false,
	const QString &already = QString(),
	const QDir &dir = QDir());
