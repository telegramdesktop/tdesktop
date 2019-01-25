/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "ui/image/image.h"

namespace Images {
class Source;
} // namespace Images

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Data {
class Session;
} // namespace Data

class AuthSession;
class mtpFileLoader;

inline uint64 mediaMix32To64(int32 a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32)
		| uint64(*reinterpret_cast<uint32*>(&b));
}

// version field removed from document.
inline MediaKey mediaKey(LocationType type, int32 dc, const uint64 &id) {
	return MediaKey(mediaMix32To64(type, dc), id);
}

inline StorageKey mediaKey(const MTPDfileLocation &location) {
	return storageKey(
		location.vdc_id.v,
		location.vvolume_id.v,
		location.vlocal_id.v);
}

struct DocumentAdditionalData {
	virtual ~DocumentAdditionalData() = default;

};

struct StickerData : public DocumentAdditionalData {
	Data::FileOrigin setOrigin() const;

	std::unique_ptr<Image> image;
	QString alt;
	MTPInputStickerSet set = MTP_inputStickerSetEmpty();
	StorageImageLocation loc; // doc thumb location
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

bool fileIsImage(const QString &name, const QString &mime);

namespace Serialize {
class Document;
} // namespace Serialize;

class DocumentData {
public:
	DocumentData(not_null<Data::Session*> owner, DocumentId id);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] AuthSession &session() const;

	void setattributes(
		const QVector<MTPDocumentAttribute> &attributes);

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item);
	void automaticLoadSettingsChanged();

	enum FilePathResolveType {
		FilePathResolveCached,
		FilePathResolveChecked,
		FilePathResolveSaveFromData,
		FilePathResolveSaveFromDataSilent,
	};
	[[nodiscard]] bool loaded(
		FilePathResolveType type = FilePathResolveCached) const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] QString loadingFilePath() const;
	[[nodiscard]] bool displayLoading() const;
	void save(
		Data::FileOrigin origin,
		const QString &toFile,
		ActionOnLoad action = ActionOnLoadNone,
		const FullMsgId &actionMsgId = FullMsgId(),
		LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal,
		bool autoLoading = false);
	void cancel();
	[[nodiscard]] bool cancelled() const;
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int32 loadOffset() const;
	[[nodiscard]] bool uploading() const;

	void setWaitingForAlbum();
	[[nodiscard]] bool waitingForAlbum() const;

	[[nodiscard]] QByteArray data() const;
	[[nodiscard]] const FileLocation &location(bool check = false) const;
	void setLocation(const FileLocation &loc);

	[[nodiscard]] QString filepath(
		FilePathResolveType type = FilePathResolveCached,
		bool forceSavingAs = false) const;

	[[nodiscard]] bool saveToCache() const;

	void performActionOnLoad();

	void unload();
	[[nodiscard]] Image *getReplyPreview(Data::FileOrigin origin);

	[[nodiscard]] StickerData *sticker() const;
	void checkStickerLarge();
	void checkStickerSmall();
	[[nodiscard]] Image *getStickerSmall();
	[[nodiscard]] Image *getStickerLarge();
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
	[[nodiscard]] int32 duration() const;
	[[nodiscard]] bool isImage() const;
	void recountIsImage();
	[[nodiscard]] bool supportsStreaming() const;
	void setData(const QByteArray &data) {
		_data = data;
	}
	bool checkWallPaperProperties();
	[[nodiscard]] bool isWallPaper() const;

	[[nodiscard]] bool hasThumbnail() const;
	void loadThumbnail(Data::FileOrigin origin);
	[[nodiscard]] Image *thumbnailInline() const;
	[[nodiscard]] Image *thumbnail() const;
	void updateThumbnails(
		ImagePtr thumbnailInline,
		ImagePtr thumbnail);

	[[nodiscard]] Image *goodThumbnail() const;
	[[nodiscard]] Storage::Cache::Key goodThumbnailCacheKey() const;
	void setGoodThumbnail(QImage &&image, QByteArray &&bytes);
	void refreshGoodThumbnail();
	void replaceGoodThumbnail(std::unique_ptr<Images::Source> &&source);

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
	void refreshStickerThumbFileReference();

	// When we have some client-side generated document
	// (for example for displaying an external inline bot result)
	// and it has downloaded data, we can collect that data from it
	// to (this) received from the server "same" document.
	void collectLocalData(not_null<DocumentData*> local);

	[[nodiscard]] QString filename() const;
	[[nodiscard]] QString mimeString() const;
	[[nodiscard]] bool hasMimeType(QLatin1String mime) const;
	void setMimeString(const QString &mime);

	[[nodiscard]] MediaKey mediaKey() const;
	[[nodiscard]] Storage::Cache::Key cacheKey() const;
	[[nodiscard]] uint8 cacheTag() const;

	[[nodiscard]] static QString ComposeNameString(
		const QString &filename,
		const QString &songTitle,
		const QString &songPerformer);
	[[nodiscard]] QString composeNameString() const;

	~DocumentData();

	DocumentId id = 0;
	DocumentType type = FileDocument;
	QSize dimensions;
	int32 date = 0;
	int32 size = 0;

	FileStatus status = FileReady;

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	friend class Serialize::Document;

	LocationType locationType() const;
	void validateGoodThumbnail();

	void destroyLoader(mtpFileLoader *newValue = nullptr) const;

	[[nodiscard]] bool thumbnailEnoughForSticker() const;

	// Two types of location: from MTProto by dc+access or from web by url
	int32 _dc = 0;
	uint64 _access = 0;
	QByteArray _fileReference;
	QString _url;
	QString _filename;
	QString _mimeString;
	WebFileLocation _urlLocation;

	ImagePtr _thumbnailInline;
	ImagePtr _thumbnail;
	std::unique_ptr<Image> _goodThumbnail;
	Data::ReplyPreview _replyPreview;

	not_null<Data::Session*> _owner;

	FileLocation _location;
	QByteArray _data;
	std::unique_ptr<DocumentAdditionalData> _additional;
	int32 _duration = -1;
	bool _isImage = false;
	bool _supportsStreaming = false;

	ActionOnLoad _actionOnLoad = ActionOnLoadNone;
	FullMsgId _actionOnLoadMsgId;
	mutable FileLoader *_loader = nullptr;

};

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit);
QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform);

class DocumentClickHandler : public FileClickHandler {
public:
	DocumentClickHandler(
		not_null<DocumentData*> document,
		FullMsgId context = FullMsgId())
	: FileClickHandler(context)
	, _document(document) {
	}
	not_null<DocumentData*> document() const {
		return _document;
	}

private:
	not_null<DocumentData*> _document;

};

class DocumentSaveClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void Save(
		Data::FileOrigin origin,
		not_null<DocumentData*> document,
		bool forceSavingAs = false);

protected:
	void onClickImpl() const override;

};

class DocumentOpenClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> document,
		HistoryItem *context,
		ActionOnLoad action = ActionOnLoadOpen);

protected:
	void onClickImpl() const override;

};

class DocumentCancelClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;

protected:
	void onClickImpl() const override;

};

class GifOpenClickHandler : public DocumentOpenClickHandler {
public:
	using DocumentOpenClickHandler::DocumentOpenClickHandler;

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

QString FileNameForSave(
	const QString &title,
	const QString &filter,
	const QString &prefix,
	QString name,
	bool savingAs,
	const QDir &dir = QDir());

namespace Data {

QString FileExtension(const QString &filepath);
bool IsValidMediaFile(const QString &filepath);
bool IsExecutableName(const QString &filepath);
base::binary_guard ReadImageAsync(
	not_null<DocumentData*> document,
	FnMut<void(QImage&&)> done);

} // namespace Data
