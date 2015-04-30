/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

typedef uint64 PeerId;
typedef uint64 PhotoId;
typedef uint64 VideoId;
typedef uint64 AudioId;
typedef uint64 DocumentId;
typedef uint64 WebPageId;
typedef int32 MsgId;

struct NotifySettings {
	NotifySettings() : mute(0), sound("default"), previews(true), events(1) {
	}
	int32 mute;
	string sound;
	bool previews;
	int32 events;
};
typedef NotifySettings *NotifySettingsPtr;

static const NotifySettingsPtr UnknownNotifySettings = NotifySettingsPtr(0);
static const NotifySettingsPtr EmptyNotifySettings = NotifySettingsPtr(1);
extern NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
extern NotifySettingsPtr globalNotifyAllPtr, globalNotifyUsersPtr, globalNotifyChatsPtr;

inline bool isNotifyMuted(NotifySettingsPtr settings, int32 *changeIn = 0) {
	if (settings != UnknownNotifySettings && settings != EmptyNotifySettings) {
		int32 t = unixtime();
		if (settings->mute > t) {
			if (changeIn) *changeIn = settings->mute - t + 1;
			return true;
		}
	}
	if (changeIn) *changeIn = 0;
	return false;
}

style::color peerColor(int32 index);
ImagePtr userDefPhoto(int32 index);
ImagePtr chatDefPhoto(int32 index);

struct ChatData;
struct UserData;
struct PeerData {
	PeerData(const PeerId &id);
	virtual ~PeerData() {
		if (notify != UnknownNotifySettings && notify != EmptyNotifySettings) {
			delete notify;
			notify = UnknownNotifySettings;
		}
	}

	UserData *asUser();
	const UserData *asUser() const;

	ChatData *asChat();
	const ChatData *asChat() const;

	void updateName(const QString &newName, const QString &newNameOrPhone, const QString &newUsername);

	void fillNames();

	virtual void nameUpdated() {
	}

	PeerId id;

	QString name;
	QString nameOrPhone;
	typedef QSet<QString> Names;
	Names names; // for filtering
	typedef QSet<QChar> NameFirstChars;
	NameFirstChars chars;

	bool loaded;
	bool chat;
	uint64 access;
	MTPinputPeer input;
	MTPinputUser inputUser;

	int32 colorIndex;
	style::color color;
	ImagePtr photo;

	int32 nameVersion;

	NotifySettingsPtr notify;
};
static const uint64 UserNoAccess = 0xFFFFFFFFFFFFFFFFULL;

class PeerLink : public ITextLink {
public:
	PeerLink(PeerData *peer) : _peer(peer) {
	}
	void onClick(Qt::MouseButton button) const;
	PeerData *peer() const {
		return _peer;
	}

private:
	PeerData *_peer;
};

struct PhotoData;
struct UserData : public PeerData {
	UserData(const PeerId &id) : PeerData(id), lnk(new PeerLink(this)), onlineTill(0), contact(-1), photosCount(-1) {
	}
	void setPhoto(const MTPUserProfilePhoto &photo);
	void setName(const QString &first, const QString &last, const QString &phoneName, const QString &username);
	void setPhone(const QString &newPhone);
	void nameUpdated();

	QString firstName;
	QString lastName;
	QString username;
	QString phone;
	Text nameText;
	PhotoId photoId;
	TextLinkPtr lnk;
	int32 onlineTill;
	int32 contact; // -1 - not contact, cant add (self, empty, deleted, foreign), 0 - not contact, can add (request), 1 - contact

	typedef QList<PhotoData*> Photos;
	Photos photos;
	int32 photosCount; // -1 not loaded, 0 all loaded
};

struct ChatData : public PeerData {
	ChatData(const PeerId &id) : PeerData(id), count(0), date(0), version(0), left(false), forbidden(true), photoId(0) {
	}
	void setPhoto(const MTPChatPhoto &photo, const PhotoId &phId = 0);
	int32 count;
	int32 date;
	int32 version;
	int32 admin;
	bool left;
	bool forbidden;
	typedef QMap<UserData*, int32> Participants;
	Participants participants;
	typedef QMap<UserData*, bool> CanKick;
	CanKick cankick;
	typedef QList<UserData*> LastAuthors;
	LastAuthors lastAuthors;
	ImagePtr photoFull;
	PhotoId photoId;
	QString invitationUrl;
	// geo
};

typedef QMap<char, QPixmap> PreparedPhotoThumbs;
struct PhotoData {
	PhotoData(const PhotoId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, const ImagePtr &thumb = ImagePtr(), const ImagePtr &medium = ImagePtr(), const ImagePtr &full = ImagePtr()) :
		id(id), access(access), user(user), date(date), thumb(thumb), medium(medium), full(full), chat(0) {
	}
	void forget() {
		thumb->forget();
		replyPreview->forget();
		medium->forget();
		full->forget();
	}
	ImagePtr makeReplyPreview() {
		if (replyPreview->isNull() && !thumb->isNull()) {
			if (thumb->loaded()) {
				int w = thumb->width(), h = thumb->height();
				if (w <= 0) w = 1;
				if (h <= 0) h = 1;
				replyPreview = ImagePtr(w > h ? thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(st::msgReplyBarSize.height()), "PNG");
			} else {
				thumb->load();
			}
		}
		return replyPreview;
	}
	PhotoId id;
	uint64 access;
	int32 user;
	int32 date;
	ImagePtr thumb, replyPreview;
	ImagePtr medium;
	ImagePtr full;
	ChatData *chat; // for chat photos connection
	// geo, caption

	int32 cachew;
	QPixmap cache;
};

class PhotoLink : public ITextLink {
public:
	PhotoLink(PhotoData *photo) : _photo(photo), _peer(0) {
	}
	PhotoLink(PhotoData *photo, PeerData *peer) : _photo(photo), _peer(peer) {
	}
	void onClick(Qt::MouseButton button) const;
	PhotoData *photo() const {
		return _photo;
	}
	PeerData *peer() const {
		return _peer;
	}

private:
	PhotoData *_photo;
	PeerData *_peer;
};

enum FileStatus {
	FileFailed = -1,
	FileUploading = 0,
	FileReady = 1,
};

struct VideoData {
	VideoData(const VideoId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, int32 duration = 0, int32 w = 0, int32 h = 0, const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);

	void forget() {
		thumb->forget();
		replyPreview->forget();
	}

	void save(const QString &toFile);

	void cancel(bool beforeDownload = false) {
		mtpFileLoader *l = loader;
		loader = 0;
		if (l) {
			l->cancel();
			l->deleteLater();
			l->rpcInvalidate();
		}
		location = FileLocation();
		if (!beforeDownload) {
			openOnSave = openOnSaveMsgId = 0;
		}
	}

	void finish() {
		if (loader->done()) {
			location = FileLocation(loader->fileType(), loader->fileName());
		}
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}

	QString already(bool check = false);

	VideoId id;
	uint64 access;
	int32 user;
	int32 date;
	int32 duration;
	int32 w, h;
	ImagePtr thumb, replyPreview;
	int32 dc, size;
	// geo, caption

	FileStatus status;
	int32 uploadOffset;

	mtpTypeId fileType;
	int32 openOnSave, openOnSaveMsgId;
	mtpFileLoader *loader;
	FileLocation location;
};

class VideoLink : public ITextLink {
public:
	VideoLink(VideoData *video) : _video(video) {
	}
	VideoData *video() const {
		return _video;
	}

private:
	VideoData *_video;
};

class VideoSaveLink : public VideoLink {
public:
	VideoSaveLink(VideoData *video) : VideoLink(video) {
	}
	static void doSave(VideoData *video, bool forceSavingAs = false);
	void onClick(Qt::MouseButton button) const;
};

class VideoOpenLink : public VideoLink {
public:
	VideoOpenLink(VideoData *video) : VideoLink(video) {
	}
	void onClick(Qt::MouseButton button) const;
};

class VideoCancelLink : public VideoLink {
public:
	VideoCancelLink(VideoData *video) : VideoLink(video) {
	}
	void onClick(Qt::MouseButton button) const;
};

struct AudioData {
	AudioData(const AudioId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, const QString &mime = QString(), int32 duration = 0, int32 dc = 0, int32 size = 0);

	void forget() {
	}

	void save(const QString &toFile);

	void cancel(bool beforeDownload = false) {
		mtpFileLoader *l = loader;
		loader = 0;
		if (l) {
			l->cancel();
			l->deleteLater();
			l->rpcInvalidate();
		}
		location = FileLocation();
		if (!beforeDownload) {
			openOnSave = openOnSaveMsgId = 0;
		}
	}

	void finish() {
		if (loader->done()) {
			location = FileLocation(loader->fileType(), loader->fileName());
			data = loader->bytes();
		}
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}

	QString already(bool check = false);

	AudioId id;
	uint64 access;
	int32 user;
	int32 date;
	QString mime;
	int32 duration;
	int32 dc;
	int32 size;

	FileStatus status;
	int32 uploadOffset;

	int32 openOnSave, openOnSaveMsgId;
	mtpFileLoader *loader;
	FileLocation location;
	QByteArray data;
	int32 md5[8];
};

class AudioLink : public ITextLink {
public:
	AudioLink(AudioData *audio) : _audio(audio) {
	}
	AudioData *audio() const {
		return _audio;
	}

private:
	AudioData *_audio;
};

class AudioSaveLink : public AudioLink {
public:
	AudioSaveLink(AudioData *audio) : AudioLink(audio) {
	}
	static void doSave(AudioData *audio, bool forceSavingAs = false);
	void onClick(Qt::MouseButton button) const;
};

class AudioOpenLink : public AudioLink {
public:
	AudioOpenLink(AudioData *audio) : AudioLink(audio) {
	}
	void onClick(Qt::MouseButton button) const;
};

class AudioCancelLink : public AudioLink {
public:
	AudioCancelLink(AudioData *audio) : AudioLink(audio) {
	}
	void onClick(Qt::MouseButton button) const;
};

enum DocumentType {
	FileDocument,
	VideoDocument,
	AudioDocument,
	StickerDocument,
	AnimatedDocument
};
struct DocumentData {
	DocumentData(const DocumentId &id, const uint64 &access = 0, int32 date = 0, const QVector<MTPDocumentAttribute> &attributes = QVector<MTPDocumentAttribute>(), const QString &mime = QString(), const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);
	void setattributes(const QVector<MTPDocumentAttribute> &attributes);

	void forget() {
		thumb->forget();
		sticker->forget();
		replyPreview->forget();
	}

	void save(const QString &toFile);

	void cancel(bool beforeDownload = false) {
		mtpFileLoader *l = loader;
		loader = 0;
		if (l) {
			l->cancel();
			l->deleteLater();
			l->rpcInvalidate();
		}
		location = FileLocation();
		if (!beforeDownload) {
			openOnSave = openOnSaveMsgId = 0;
		}
	}

	void finish() {
		if (loader->done()) {
			location = FileLocation(loader->fileType(), loader->fileName());
			data = loader->bytes();
		}
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}

	QString already(bool check = false);

	DocumentId id;
	DocumentType type;
	QSize dimensions;
	int32 duration;
	uint64 access;
	int32 date;
	QString name, mime, alt; // alt - for stickers
	ImagePtr thumb, replyPreview;
	int32 dc;
	int32 size;

	FileStatus status;
	int32 uploadOffset;

	int32 openOnSave, openOnSaveMsgId;
	mtpFileLoader *loader;
	FileLocation location;

	QByteArray data;
	ImagePtr sticker;

	int32 md5[8];
};

class DocumentLink : public ITextLink {
public:
	DocumentLink(DocumentData *document) : _document(document) {
	}
	DocumentData *document() const {
		return _document;
	}

private:
	DocumentData *_document;
};

class DocumentSaveLink : public DocumentLink {
public:
	DocumentSaveLink(DocumentData *document) : DocumentLink(document) {
	}
	static void doSave(DocumentData *document, bool forceSavingAs = false);
	void onClick(Qt::MouseButton button) const;
};

class DocumentOpenLink : public DocumentLink {
public:
	DocumentOpenLink(DocumentData *document) : DocumentLink(document) {
	}
	void onClick(Qt::MouseButton button) const;
};

class DocumentCancelLink : public DocumentLink {
public:
	DocumentCancelLink(DocumentData *document) : DocumentLink(document) {
	}
	void onClick(Qt::MouseButton button) const;
};

enum WebPageType {
	WebPagePhoto,
	WebPageVideo,
	WebPageProfile,
	WebPageArticle
};
inline WebPageType toWebPageType(const QString &type) {
	if (type == QLatin1String("photo")) return WebPagePhoto;
	if (type == QLatin1String("video")) return WebPageVideo;
	if (type == QLatin1String("profile")) return WebPageProfile;
	return WebPageArticle;
}

struct WebPageData {
	WebPageData(const WebPageId &id, WebPageType type = WebPageArticle, const QString &url = QString(), const QString &displayUrl = QString(), const QString &siteName = QString(), const QString &title = QString(), const QString &description = QString(), PhotoData *photo = 0, int32 duration = 0, const QString &author = QString(), int32 pendingTill = 0);
	
	void forget() {
		if (photo) photo->forget();
	}

	WebPageId id;
	WebPageType type;
	QString url, displayUrl, siteName, title, description;
	int32 duration;
	QString author;
	PhotoData *photo;
	int32 pendingTill;
};

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir = QDir());
MsgId clientMsgId();

struct MessageCursor {
	MessageCursor() : position(0), anchor(0), scroll(QFIXED_MAX) {
	}
	MessageCursor(int position, int anchor, int scroll) : position(position), anchor(anchor), scroll(scroll) {
	}
	MessageCursor(const QTextEdit &edit) {
		fillFrom(edit);
	}
	void fillFrom(const QTextEdit &edit) {
		QTextCursor c = edit.textCursor();
		position = c.position();
		anchor = c.anchor();
		QScrollBar *s = edit.verticalScrollBar();
		scroll = s ? s->value() : QFIXED_MAX;
	}
	void applyTo(QTextEdit &edit, bool *lock = 0) {
		if (lock) *lock = true;
		QTextCursor c = edit.textCursor();
		c.setPosition(anchor, QTextCursor::MoveAnchor);
		c.setPosition(position, QTextCursor::KeepAnchor);
		edit.setTextCursor(c);
		QScrollBar *s = edit.verticalScrollBar();
		if (s) s->setValue(scroll);
		if (lock) *lock = false;
	}
	int position, anchor, scroll;
};
