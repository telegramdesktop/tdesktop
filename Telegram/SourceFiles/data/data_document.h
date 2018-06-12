/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class AuthSession;

inline uint64 mediaMix32To64(int32 a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32)
		| uint64(*reinterpret_cast<uint32*>(&b));
}

// Old method, should not be used anymore.
//inline MediaKey mediaKey(LocationType type, int32 dc, const uint64 &id) {
//	return MediaKey(mediaMix32To64(type, dc), id);
//}
// New method when version was introduced, type is not relevant anymore (all files are Documents).
inline MediaKey mediaKey(
		LocationType type,
		int32 dc,
		const uint64 &id,
		int32 version) {
	return (version > 0) ? MediaKey(mediaMix32To64(version, dc), id) : MediaKey(mediaMix32To64(type, dc), id);
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
	ImagePtr img;
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
	DocumentData(DocumentId id, not_null<AuthSession*> session);

	not_null<AuthSession*> session() const;

	void setattributes(
		const QVector<MTPDocumentAttribute> &attributes);

	void automaticLoad(const HistoryItem *item); // auto load sticker or video
	void automaticLoadSettingsChanged();

	enum FilePathResolveType {
		FilePathResolveCached,
		FilePathResolveChecked,
		FilePathResolveSaveFromData,
		FilePathResolveSaveFromDataSilent,
	};
	bool loaded(
		FilePathResolveType type = FilePathResolveCached) const;
	bool loading() const;
	QString loadingFilePath() const;
	bool displayLoading() const;
	void save(
		const QString &toFile,
		ActionOnLoad action = ActionOnLoadNone,
		const FullMsgId &actionMsgId = FullMsgId(),
		LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal,
		bool autoLoading = false);
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	void setWaitingForAlbum();
	bool waitingForAlbum() const;

	QByteArray data() const;
	const FileLocation &location(bool check = false) const;
	void setLocation(const FileLocation &loc);

	QString filepath(
		FilePathResolveType type = FilePathResolveCached,
		bool forceSavingAs = false) const;

	bool saveToCache() const;

	void performActionOnLoad();

	void forget();
	ImagePtr makeReplyPreview();

	StickerData *sticker() const;
	void checkSticker();
	bool isStickerSetInstalled() const;
	SongData *song();
	const SongData *song() const;
	VoiceData *voice();
	const VoiceData *voice() const;

	bool isVoiceMessage() const;
	bool isVideoMessage() const;
	bool isSong() const;
	bool isAudioFile() const;
	bool isVideoFile() const;
	bool isAnimation() const;
	bool isGifv() const;
	bool isTheme() const;
	bool isSharedMediaMusic() const;
	int32 duration() const;
	bool isImage() const;
	void recountIsImage();
	void setData(const QByteArray &data) {
		_data = data;
	}

	bool hasGoodStickerThumb() const;

	bool setRemoteVersion(int32 version); // Returns true if version has changed.
	void setRemoteLocation(int32 dc, uint64 access);
	void setContentUrl(const QString &url);
	void setWebLocation(const WebFileLocation &location);
	bool hasRemoteLocation() const;
	bool hasWebLocation() const;
	bool isValid() const;
	MTPInputDocument mtpInput() const;

	// When we have some client-side generated document
	// (for example for displaying an external inline bot result)
	// and it has downloaded data, we can collect that data from it
	// to (this) received from the server "same" document.
	void collectLocalData(DocumentData *local);

	QString filename() const;
	QString mimeString() const;
	bool hasMimeType(QLatin1String mime) const;
	void setMimeString(const QString &mime);

	MediaKey mediaKey() const;

	static QString ComposeNameString(
		const QString &filename,
		const QString &songTitle,
		const QString &songPerformer);
	QString composeNameString() const;

	~DocumentData();

	DocumentId id = 0;
	DocumentType type = FileDocument;
	QSize dimensions;
	int32 date = 0;
	ImagePtr thumb, replyPreview;
	int32 size = 0;

	FileStatus status = FileReady;

	std::unique_ptr<Data::UploadState> uploadingData;

private:
	friend class Serialize::Document;

	LocationType locationType() const;

	void destroyLoaderDelayed(mtpFileLoader *newValue = nullptr) const;

	// Two types of location: from MTProto by dc+access+version or from web by url
	int32 _dc = 0;
	uint64 _access = 0;
	int32 _version = 0;
	QString _url;
	QString _filename;
	QString _mimeString;
	WebFileLocation _urlLocation;

	not_null<AuthSession*> _session;

	FileLocation _location;
	QByteArray _data;
	std::unique_ptr<DocumentAdditionalData> _additional;
	int32 _duration = -1;

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
	static void doSave(
		not_null<DocumentData*> document,
		bool forceSavingAs = false);

protected:
	void onClickImpl() const override;

};

class DocumentOpenClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void doOpen(
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
