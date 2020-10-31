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
#include "ui/image/image.h"

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

struct StickerData : public DocumentAdditionalData {
	Data::FileOrigin setOrigin() const;

	bool animated = false;
	QString alt;
	MTPInputStickerSet set = MTP_inputStickerSetEmpty();
};

struct SongData : public DocumentAdditionalData {
	int32 duration = 0;
	QString title, performer;

};

struct VoiceData : public DocumentAdditionalData {
	~VoiceData();

	int duration = 0;
	VoiceWaveform waveform;
	char wavemax = 0;
};

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
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int loadOffset() const;
	[[nodiscard]] bool uploading() const;
	[[nodiscard]] bool loadedInMediaCache() const;
	void setLoadedInMediaCache(bool loaded);

	void setWaitingForAlbum();
	[[nodiscard]] bool waitingForAlbum() const;

	[[nodiscard]] const Core::FileLocation &location(bool check = false) const;
	void setLocation(const Core::FileLocation &loc);

	bool saveFromData();
	bool saveFromDataSilent();
	[[nodiscard]] QString filepath(bool check = false) const;

	[[nodiscard]] bool saveToCache() const;

	[[nodiscard]] Image *getReplyPreview(Data::FileOrigin origin);
	[[nodiscard]] bool replyPreviewLoaded() const;

	[[nodiscard]] StickerData *sticker() const;
	[[nodiscard]] Data::FileOrigin stickerSetOrigin() const;
	[[nodiscard]] Data::FileOrigin stickerOrGifOrigin() const;
	[[nodiscard]] bool isStickerSetInstalled() const;
	[[nodiscard]] SongData *song();
	[[nodiscard]] const SongData *song() const;
	[[nodiscard]] VoiceData *voice();
	[[nodiscard]] const VoiceData *voice() const;

	[[nodiscard]] bool isVoiceMessage() const;
	[[nodiscard]] bool isVideoMessage() const;
	[[nodiscard]] bool isSong() const;
	[[nodiscard]] bool isAudioFile() const;
	[[nodiscard]] bool isVideoFile() const;
	[[nodiscard]] bool isAnimation() const;
	[[nodiscard]] bool isGifv() const;
	[[nodiscard]] bool isTheme() const;
	[[nodiscard]] bool isSharedMediaMusic() const;
	[[nodiscard]] TimeId getDuration() const;
	[[nodiscard]] bool isImage() const;
	void recountIsImage();
	[[nodiscard]] bool supportsStreaming() const;
	void setNotSupportsStreaming();
	void setDataAndCache(const QByteArray &data);
	bool checkWallPaperProperties();
	[[nodiscard]] bool isWallPaper() const;
	[[nodiscard]] bool isPatternWallPaper() const;

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
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail);

	[[nodiscard]] QByteArray inlineThumbnailBytes() const {
		return _inlineThumbnailBytes;
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
	[[nodiscard]] bool hasMimeType(QLatin1String mime) const;
	void setMimeString(const QString &mime);

	[[nodiscard]] bool hasAttachedStickers() const;

	[[nodiscard]] MediaKey mediaKey() const;
	[[nodiscard]] Storage::Cache::Key cacheKey() const;
	[[nodiscard]] uint8 cacheTag() const;

	[[nodiscard]] QString composeNameString() const;

	[[nodiscard]] bool canBeStreamed() const;
	[[nodiscard]] auto createStreamingLoader(
		Data::FileOrigin origin,
		bool forceRemoteLoader) const
	-> std::unique_ptr<Media::Streaming::Loader>;
	[[nodiscard]] bool useStreamingLoader() const;

	void setInappPlaybackFailed();
	[[nodiscard]] bool inappPlaybackFailed() const;

	DocumentId id = 0;
	DocumentType type = FileDocument;
	QSize dimensions;
	int32 date = 0;
	int32 size = 0;

	FileStatus status = FileReady;

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	enum class Flag : uchar {
		StreamingMaybeYes = 0x01,
		StreamingMaybeNo = 0x02,
		StreamingPlaybackFailed = 0x04,
		ImageType = 0x08,
		DownloadCancelled = 0x10,
		LoadedInMediaCache = 0x20,
		HasAttachedStickers = 0x40,
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

	void finishLoad();
	void handleLoaderUpdates();
	void destroyLoader();

	bool saveFromDataChecked();

	const not_null<Data::Session*> _owner;

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

	Core::FileLocation _location;
	std::unique_ptr<DocumentAdditionalData> _additional;
	int32 _duration = -1;
	mutable Flags _flags = kStreamingSupportedUnknown;
	GoodThumbnailState _goodThumbnailState = GoodThumbnailState();
	std::unique_ptr<FileLoader> _loader;

};

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit);
QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform);

class DocumentClickHandler : public FileClickHandler {
public:
	DocumentClickHandler(
		not_null<DocumentData*> document,
		FullMsgId context = FullMsgId());

	[[nodiscard]] not_null<DocumentData*> document() const {
		return _document;
	}

private:
	const not_null<DocumentData*> _document;

};

class DocumentSaveClickHandler : public DocumentClickHandler {
public:
	enum class Mode {
		ToCacheOrFile,
		ToFile,
		ToNewFile,
	};
	using DocumentClickHandler::DocumentClickHandler;
	static void Save(
		Data::FileOrigin origin,
		not_null<DocumentData*> document,
		Mode mode = Mode::ToCacheOrFile);

protected:
	void onClickImpl() const override;

};

class DocumentOpenClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> document,
		HistoryItem *context);

protected:
	void onClickImpl() const override;

};

class DocumentCancelClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;

protected:
	void onClickImpl() const override;

};

class DocumentOpenWithClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);

protected:
	void onClickImpl() const override;

};

class VoiceSeekClickHandler : public DocumentOpenClickHandler {
public:
	using DocumentOpenClickHandler::DocumentOpenClickHandler;

protected:
	void onClickImpl() const override {
	}

};

class DocumentWrappedClickHandler : public DocumentClickHandler {
public:
	DocumentWrappedClickHandler(
		ClickHandlerPtr wrapped,
		not_null<DocumentData*> document,
		FullMsgId context = FullMsgId())
	: DocumentClickHandler(document, context)
	, _wrapped(wrapped) {
	}

protected:
	void onClickImpl() const override {
		_wrapped->onClick({ Qt::LeftButton });
	}

private:
	ClickHandlerPtr _wrapped;

};

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

namespace Data {

[[nodiscard]] QString FileExtension(const QString &filepath);
[[nodiscard]] bool IsValidMediaFile(const QString &filepath);
[[nodiscard]] bool IsExecutableName(const QString &filepath);
[[nodiscard]] bool IsIpRevealingName(const QString &filepath);
base::binary_guard ReadImageAsync(
	not_null<Data::DocumentMedia*> media,
	FnMut<QImage(QImage)> postprocess,
	FnMut<void(QImage&&)> done);

} // namespace Data
