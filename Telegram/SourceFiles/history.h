/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

typedef uint64 PeerId;
typedef uint64 PhotoId;
typedef uint64 VideoId;
typedef uint64 AudioId;
typedef uint64 DocumentId;
typedef int32 MsgId;

void historyInit();

class HistoryItem;

void startGif(HistoryItem *row, const QString &file);
void itemRemovedGif(HistoryItem *item);
void itemReplacedGif(HistoryItem *oldItem, HistoryItem *newItem);
void stopGif();

static const uint32 FullItemSel = 0xFFFFFFFF;

typedef QMap<int32, HistoryItem*> SelectedItemSet;

extern TextParseOptions _textNameOptions, _textDlgOptions;

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

inline bool isNotifyMuted(NotifySettingsPtr settings) {
	if (settings == UnknownNotifySettings || settings == EmptyNotifySettings) {
		return false;
	}
	return (settings->mute > unixtime());
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
	ImagePtr photoFull;
	PhotoId photoId;
	// geo
};

typedef QMap<char, QPixmap> PreparedPhotoThumbs;
struct PhotoData {
	PhotoData(const PhotoId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, const ImagePtr &thumb = ImagePtr(), const ImagePtr &medium = ImagePtr(), const ImagePtr &full = ImagePtr()) :
		id(id), access(access), user(user), date(date), thumb(thumb), medium(medium), full(full), chat(0) {
	}
	void forget() {
		thumb->forget();
		medium->forget();
		full->forget();
	}
	PhotoId id;
	uint64 access;
	int32 user;
	int32 date;
	ImagePtr thumb;
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
	VideoData(const VideoId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, int32 duration = 0, int32 w = 0, int32 h = 0, const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0) :
		id(id), access(access), user(user), date(date), duration(duration), w(w), h(h), thumb(thumb), dc(dc), size(size), status(FileReady), uploadOffset(0), fileType(0), openOnSave(0), openOnSaveMsgId(0), loader(0) {
		memset(md5, 0, sizeof(md5));
	}
	void forget() {
		thumb->forget();
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
		fileName = QString();
		modDate = QDateTime();
		if (!beforeDownload) {
			openOnSave = openOnSaveMsgId = 0;
		}
	}

	void finish() {
		if (loader->done()) {
			fileName = loader->fileName();
			modDate = fileName.isEmpty() ? QDateTime() : QFileInfo(fileName).lastModified();
		}
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}

	QString already(bool check = false) {
		if (!check) return fileName;

		QString res = modDate.isNull() ? QString() : fileName;
		if (!res.isEmpty()) {
			QFileInfo f(res);
			if (f.exists() && f.lastModified() <= modDate) {
				return res;
			}
		}
		return QString();
	}

	VideoId id;
	uint64 access;
	int32 user;
	int32 date;
	int32 duration;
	int32 w, h;
	ImagePtr thumb;
	int32 dc, size;
	// geo, caption

	FileStatus status;
	int32 uploadOffset;

	mtpTypeId fileType;
	int32 openOnSave, openOnSaveMsgId;
	mtpFileLoader *loader;
	QString fileName;
	QDateTime modDate;
	int32 md5[8];
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
	void doSave(bool forceSavingAs = false) const;
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
	AudioData(const AudioId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, int32 duration = 0, int32 dc = 0, int32 size = 0) : 
		id(id), access(access), user(user), date(date), duration(duration), dc(dc), size(size), status(FileReady), uploadOffset(0), openOnSave(0), openOnSaveMsgId(0), loader(0) {
		memset(md5, 0, sizeof(md5));
	}
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
		fileName = QString();
		modDate = QDateTime();
		if (!beforeDownload) {
			openOnSave = openOnSaveMsgId = 0;
		}
	}

	void finish() {
		if (loader->done()) {
			fileName = loader->fileName();
			modDate = fileName.isEmpty() ? QDateTime() : QFileInfo(fileName).lastModified();
			data = loader->bytes();
		}
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}

	QString already(bool check = false) {
		if (!check) return fileName;

		QString res = modDate.isNull() ? QString() : fileName;
		if (!res.isEmpty()) {
			QFileInfo f(res);
			if (f.exists() && f.lastModified() <= modDate) {
				return res;
			}
		}
		return QString();
	}

	AudioId id;
	uint64 access;
	int32 user;
	int32 date;
	int32 duration;
	int32 dc;
	int32 size;

	FileStatus status;
	int32 uploadOffset;

	int32 openOnSave, openOnSaveMsgId;
	mtpFileLoader *loader;
	QString fileName;
	QDateTime modDate;
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
	void doSave(bool forceSavingAs = false) const;
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

struct DocumentData {
	DocumentData(const DocumentId &id, const uint64 &access = 0, int32 user = 0, int32 date = 0, const QString &name = QString(), const QString &mime = QString(), const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0) :
		id(id), access(access), user(user), date(date), name(name), mime(mime), thumb(thumb), dc(dc), size(size), status(FileReady), uploadOffset(0), openOnSave(0), openOnSaveMsgId(0), loader(0) {
		memset(md5, 0, sizeof(md5));
	}
	void forget() {
		thumb->forget();
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
		fileName = QString();
		modDate = QDateTime();
		if (!beforeDownload) {
			openOnSave = openOnSaveMsgId = 0;
		}
	}

	void finish() {
		if (loader->done()) {
			fileName = loader->fileName();
			modDate = fileName.isEmpty() ? QDateTime() : QFileInfo(fileName).lastModified();
		}
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}

	QString already(bool check = false) {
		if (!check) return fileName;

		QString res = modDate.isNull() ? QString() : fileName;
		if (!res.isEmpty()) {
			QFileInfo f(res);
			if (f.exists() && f.lastModified() <= modDate) {
				return res;
			}
		}
		return QString();
	}

	DocumentId id;
	uint64 access;
	int32 user;
	int32 date;
	QString name, mime;
	ImagePtr thumb;
	int32 dc;
	int32 size;

	FileStatus status;
	int32 uploadOffset;

	int32 openOnSave, openOnSaveMsgId;
	mtpFileLoader *loader;
	QString fileName;
	QDateTime modDate;
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
	void doSave(bool forceSavingAs = false) const;
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

MsgId clientMsgId();

struct History;
struct Histories : public QHash<PeerId, History*> {
	typedef QHash<PeerId, History*> Parent;

	Histories() : unreadFull(0), unreadMuted(0) {
	}

	void clear();
	Parent::iterator erase(Parent::iterator i);
	~Histories() {
		clear();

		unreadFull = unreadMuted = 0;
	}

	HistoryItem *addToBack(const MTPmessage &msg, int msgState = 1); // 1 - new message, 0 - not new message, -1 - searched message
//	HistoryItem *addToBack(const MTPgeoChatMessage &msg, bool newMsg = true);

	typedef QMap<History*, uint64> TypingHistories; // when typing in this history started
	TypingHistories typing;

	int32 unreadFull, unreadMuted;
};

struct HistoryBlock;

struct DialogRow {
	DialogRow(History *history = 0, DialogRow *prev = 0, DialogRow *next = 0, int32 pos = 0) : prev(prev), next(next), history(history), pos(pos), attached(0) {
	}

	void paint(QPainter &p, int32 w, bool act, bool sel) const;

	DialogRow *prev, *next;
	History *history;
	int32 pos;
	void *attached; // for any attached data, for example View in contacts list
};

struct FakeDialogRow {
	FakeDialogRow(HistoryItem *item) : _item(item), _cacheFor(0), _cache(st::dlgRichMinWidth) {
	}

	void paint(QPainter &p, int32 w, bool act, bool sel) const;

	HistoryItem *_item;
	mutable const HistoryItem *_cacheFor;
	mutable Text _cache;
};

enum HistoryMediaType {
	MediaTypePhoto,
	MediaTypeVideo,
	MediaTypeGeo,
	MediaTypeContact,
	MediaTypeAudio,
	MediaTypeDocument,
	MediaTypeImageLink,

	MediaTypeCount
};

enum MediaOverviewType {
	OverviewPhotos,
	OverviewVideos,
	OverviewDocuments,
	OverviewAudios,

	OverviewCount
};

inline MediaOverviewType mediaToOverviewType(HistoryMediaType t) {
	switch (t) {
	case MediaTypePhoto: return OverviewPhotos;
	case MediaTypeVideo: return OverviewVideos;
	case MediaTypeDocument: return OverviewDocuments;
	case MediaTypeAudio: return OverviewAudios;
	}
	return OverviewCount;
}

inline HistoryMediaType overviewToMediaType(MediaOverviewType t) {
	switch (t) {
	case OverviewPhotos: return MediaTypePhoto;
	case OverviewVideos: return MediaTypeVideo;
	case OverviewAudios: return MediaTypeAudio;
	case OverviewDocuments: return MediaTypeDocument;
	}
	return MediaTypeCount;
}

inline MTPMessagesFilter typeToMediaFilter(MediaOverviewType &type) {
	switch (type) {
	case OverviewPhotos: return MTP_inputMessagesFilterPhotos();
	case OverviewVideos: return MTP_inputMessagesFilterVideo();
	case OverviewDocuments: return MTP_inputMessagesFilterDocument();
	case OverviewAudios: return MTP_inputMessagesFilterAudio();
	default: type = OverviewCount; break;
	}
	return MTPMessagesFilter();
}

class HistoryMedia;
class HistoryMessage;
class HistoryUnreadBar;
struct History : public QList<HistoryBlock*> {
	History(const PeerId &peerId);

	typedef QList<HistoryBlock*> Parent;
	void clear(bool leaveItems = false);
	Parent::iterator erase(Parent::iterator i);
	void blockResized(HistoryBlock *block, int32 dh);
	void removeBlock(HistoryBlock *block);

	~History() {
		clear();
	}

	HistoryItem *createItem(HistoryBlock *block, const MTPmessage &msg, bool newMsg, bool returnExisting = false);
	HistoryItem *createItemForwarded(HistoryBlock *block, MsgId id, HistoryMessage *msg);
//	HistoryItem *createItem(HistoryBlock *block, const MTPgeoChatMessage &msg, bool newMsg);
	HistoryItem *addToBackService(MsgId msgId, QDateTime date, const QString &text, bool out = false, bool unread = false, HistoryMedia *media = 0, bool newMsg = true);
	HistoryItem *addToBack(const MTPmessage &msg, bool newMsg = true);
	HistoryItem *addToHistory(const MTPmessage &msg);
	HistoryItem *addToBackForwarded(MsgId id, HistoryMessage *item);
//	HistoryItem *addToBack(const MTPgeoChatMessage &msg, bool newMsg = true);
	void addToFront(const QVector<MTPMessage> &slice);
	void addToBack(const QVector<MTPMessage> &slice);
	void createInitialDateBlock(const QDateTime &date);
	HistoryItem *doAddToBack(HistoryBlock *to, bool newBlock, HistoryItem *adding, bool newMsg);

	void newItemAdded(HistoryItem *item);
	void unregTyping(UserData *from);

	void inboxRead(HistoryItem *wasRead);
	void outboxRead(HistoryItem *wasRead);

	void setUnreadCount(int32 newUnreadCount, bool psUpdate = true);
	void setMsgCount(int32 newMsgCount);
	void setMute(bool newMute);
	void getNextShowFrom(HistoryBlock *block, int32 i);
	void addUnreadBar();
	void clearNotifications();

	bool readyForWork() const; // all unread loaded or loaded around activeMsgId
	bool loadedAtBottom() const; // last message is in the list
	bool loadedAtTop() const; // nothing was added after loading history back

	void fixLastMessage(bool wasAtBottom);

	void loadAround(MsgId msgId);
	bool canShowAround(MsgId msgId) const;

	MsgId minMsgId() const;
	MsgId maxMsgId() const;

	int32 geomResize(int32 newWidth, int32 *ytransform = 0, bool dontRecountText = false); // return new size
	int32 width, height, msgCount, unreadCount;
	int32 inboxReadTill, outboxReadTill;
	HistoryItem *showFrom;
	HistoryUnreadBar *unreadBar;

	PeerData *peer;
	bool oldLoaded, newLoaded;
	HistoryItem *last;
	MsgId activeMsgId;

	typedef QList<HistoryItem*> NotifyQueue;
	NotifyQueue notifies;

	void removeNotification(HistoryItem *item) {
		if (!notifies.isEmpty()) {
			for (NotifyQueue::iterator i = notifies.begin(), e = notifies.end(); i != e; ++i) {
				if ((*i) == item) {
					notifies.erase(i);
					break;
				}
			}
		}
	}
	HistoryItem *currentNotification() {
		return notifies.isEmpty() ? 0 : notifies.front();
	}
	void skipNotification() {
		if (!notifies.isEmpty()) {
			notifies.pop_front();
		}
	}

	void itemReplaced(HistoryItem *old, HistoryItem *item) {
		if (!notifies.isEmpty()) {
			for (NotifyQueue::iterator i = notifies.begin(), e = notifies.end(); i != e; ++i) {
				if ((*i) == old) {
					*i = item;
					break;
				}
			}
		}
		if (last == old) {
			last = item;
		}
		// showFrom can't be detached
	}

	QString draft;
	QTextCursor draftCur;
	int32 lastWidth, lastScrollTop;
	bool mute;

	mtpRequestId sendRequestId;

	// for dialog drawing
	Text nameText;
	void updateNameText();

	mutable const HistoryItem *textCachedFor; // cache
	mutable Text lastItemTextCache;

	void paintDialog(QPainter &p, int32 w, bool sel) const;

	typedef QMap<QChar, DialogRow*> DialogLinks;
	DialogLinks dialogs;
	int32 posInDialogs;

	typedef QMap<UserData*, uint64> TypingUsers;
	TypingUsers typing;
	QString typingStr;
	Text typingText;
	uint32 typingFrame;
	bool updateTyping(uint64 ms = 0, uint32 dots = 0, bool force = false);
	uint64 myTyping;

	typedef QList<MsgId> MediaOverview;
	typedef QMap<MsgId, NullType> MediaOverviewIds;

	MediaOverview _overview[OverviewCount];
	MediaOverviewIds _overviewIds[OverviewCount];
	int32 _overviewCount[OverviewCount]; // -1 - not loaded, 0 - all loaded, > 0 - count, but not all loaded

	static const int32 ScrollMax = INT_MAX;
};

struct DialogsList {
	DialogsList(bool sortByName) : begin(&last), end(&last), byName(sortByName), count(0), current(&last) {
	}

	void adjustCurrent(int32 y, int32 h) const {
		int32 pos = (y > 0) ? (y / h) : 0;
		while (current->pos > pos && current != begin) {
			current = current->prev;
		}
		while (current->pos + 1 <= pos && current->next != end) {
			current = current->next;
		}
	}

	void paint(QPainter &p, int32 w, int32 hFrom, int32 hTo, PeerData *act, PeerData *sel) const {
		adjustCurrent(hFrom, st::dlgHeight);

		DialogRow *drawFrom = current;
		p.translate(0, drawFrom->pos * st::dlgHeight);
		while (drawFrom != end && drawFrom->pos * st::dlgHeight < hTo) {
			drawFrom->paint(p, w, (drawFrom->history->peer == act), (drawFrom->history->peer == sel));
			drawFrom = drawFrom->next;
			p.translate(0, st::dlgHeight);
		}
	}

	DialogRow *rowAtY(int32 y, int32 h) const {
		if (!count) return 0;

		int32 pos = (y > 0) ? (y / h) : 0;
		adjustCurrent(y, h);
		return (pos == current->pos) ? current : 0;
	}

	DialogRow *addToEnd(History *history, bool updatePos = true) {
		DialogRow *result = new DialogRow(history, end->prev, end, end->pos);
		end->pos++;
		if (begin == end) {
			begin = current = result;
			if (!byName && updatePos) history->posInDialogs = 0;
		} else {
			end->prev->next = result;
			if (!byName && updatePos) history->posInDialogs = end->prev->history->posInDialogs + 1;
		}
		rowByPeer.insert(history->peer->id, result);
		++count;
		return (end->prev = result);
	}

	void bringToTop(DialogRow *row, bool updatePos = true) {
		if (!byName && updatePos && row != begin) {
			row->history->posInDialogs = begin->history->posInDialogs - 1;
		}
		insertBefore(row, begin);
	}

	bool insertBefore(DialogRow *row, DialogRow *before) {
		if (row == before) return false;

		if (current == row) current = row->prev;

		DialogRow *updateTill = row->prev;
		remove(row);

		// insert row
		row->next = before; // update row
		row->prev = before->prev;
		row->next->prev = row; // update row->next
		if (row->prev) { // update row->prev
			row->prev->next = row;
		} else {
			begin = row;
		}

		// update y
		for (DialogRow *n = row; n != updateTill; n = n->next) {
			n->next->pos++;
			row->pos--;
		}
		return true;
	}

	bool insertAfter(DialogRow *row, DialogRow *after) {
		if (row == after) return false;

		if (current == row) current = row->next;

		DialogRow *updateFrom = row->next;
		remove(row);

		// insert row
		row->prev = after; // update row
		row->next = after->next;
		row->prev->next = row; // update row->prev
		row->next->prev = row; // update row->next

		// update y
		for (DialogRow *n = updateFrom; n != row; n = n->next) {
			n->pos--;
			row->pos++;
		}
		return true;
	}

	DialogRow *adjustByName(const PeerData *peer) {
		if (!byName) return 0;

		RowByPeer::iterator i = rowByPeer.find(peer->id);
		if (i == rowByPeer.cend()) return 0;

		DialogRow *row = i.value(), *change = row;
		while (change->prev && change->prev->history->peer->name > peer->name) {
			change = change->prev;
		}
		if (!insertBefore(row, change)) {
			while (change->next != end && change->next->history->peer->name < peer->name) {
				change = change->next;
			}
			insertAfter(row, change);
		}
		return row;
	}

	DialogRow *addByName(History *history) {
		if (!byName) return 0;

		DialogRow *row = addToEnd(history), *change = row;
		const QString &peerName(history->peer->name);
		while (change->prev && change->prev->history->peer->name > peerName) {
			change = change->prev;
		}
		if (!insertBefore(row, change)) {
			while (change->next != end && change->next->history->peer->name < peerName) {
				change = change->next;
			}
			insertAfter(row, change);
		}
		return row;
	}

	void adjustByPos(DialogRow *row) {
		if (byName) return;

		DialogRow *change = row;
		while (change->prev && change->prev->history->posInDialogs > row->history->posInDialogs) {
			change = change->prev;
		}
		if (!insertBefore(row, change)) {
			while (change->next != end && change->next->history->posInDialogs < row->history->posInDialogs) {
				change = change->next;
			}
			insertAfter(row, change);
		}
	}

	DialogRow *addByPos(History *history) {
		if (byName) return 0;

		DialogRow *row = addToEnd(history, false);
		adjustByPos(row);
		return row;
	}

	bool del(const PeerId &peerId, DialogRow *replacedBy = 0);

	void remove(DialogRow *row) {
		row->next->prev = row->prev; // update row->next
		if (row->prev) { // update row->prev
			row->prev->next = row->next;
		} else {
			begin = row->next;
		}
	}

	void clear() {
		while (begin != end) {
			current = begin;
			begin = begin->next;
			delete current;
		}
		current = begin;
		rowByPeer.clear();
		count = 0;
	}

	~DialogsList() {
		clear();
	}

	DialogRow last;
	DialogRow *begin, *end;
	bool byName;
	int32 count;

	typedef QHash<PeerId, DialogRow*> RowByPeer;
	RowByPeer rowByPeer;

	mutable DialogRow *current; // cache
};

struct DialogsIndexed {
	DialogsIndexed(bool sortByName) : byName(sortByName), list(byName) {
	}

	History::DialogLinks addToEnd(History *history) {
		History::DialogLinks result;
		DialogsList::RowByPeer::const_iterator i = list.rowByPeer.find(history->peer->id);
		if (i != list.rowByPeer.cend()) {
			return i.value()->history->dialogs;
		}

		result.insert(0, list.addToEnd(history));
		for (PeerData::NameFirstChars::const_iterator i = history->peer->chars.cbegin(), e = history->peer->chars.cend(); i != e; ++i) {
			DialogsIndex::iterator j = index.find(*i);
			if (j == index.cend()) {
				j = index.insert(*i, new DialogsList(byName));
			}
			result.insert(*i, j.value()->addToEnd(history));
		}

		return result;
	}

	DialogRow *addByName(History *history) {
		DialogsList::RowByPeer::const_iterator i = list.rowByPeer.constFind(history->peer->id);
		if (i != list.rowByPeer.cend()) {
			return i.value();
		}

		DialogRow *res = list.addByName(history);
		for (PeerData::NameFirstChars::const_iterator i = history->peer->chars.cbegin(), e = history->peer->chars.cend(); i != e; ++i) {
			DialogsIndex::iterator j = index.find(*i);
			if (j == index.cend()) {
				j = index.insert(*i, new DialogsList(byName));
			}
			j.value()->addByName(history);
		}
		return res;
	}

	void bringToTop(const History::DialogLinks &links) {
		for (History::DialogLinks::const_iterator i = links.cbegin(), e = links.cend(); i != e; ++i) {
			if (i.key() == QChar(0)) {
				list.bringToTop(i.value());
			} else {
				DialogsIndex::iterator j = index.find(i.key());
				if (j != index.cend()) {
					j.value()->bringToTop(i.value());
				}
			}
		}
	}

	void peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

	void del(const PeerData *peer, DialogRow *replacedBy = 0) {
		if (list.del(peer->id, replacedBy)) {
			for (PeerData::NameFirstChars::const_iterator i = peer->chars.cbegin(), e = peer->chars.cend(); i != e; ++i) {
				DialogsIndex::iterator j = index.find(*i);
				if (j != index.cend()) {
					j.value()->del(peer->id, replacedBy);
				}
			}
		}
	}

	~DialogsIndexed() {
		clear();
	}

	void clear();

	bool byName;
	DialogsList list;
	typedef QMap<QChar, DialogsList*> DialogsIndex;
	DialogsIndex index;
};

struct HistoryBlock : public QVector<HistoryItem*> {
	HistoryBlock(History *hist) : y(0), height(0), history(hist) {
	}

	typedef QVector<HistoryItem*> Parent;
	void clear(bool leaveItems = false);
	Parent::iterator erase(Parent::iterator i);
	~HistoryBlock() {
		clear();
	}
	void removeItem(HistoryItem *item);

	int32 geomResize(int32 newWidth, int32 *ytransform, bool dontRecountText); // return new size
	int32 y, height;
	History *history;
};

class HistoryElem {
public:

	HistoryElem() : _height(0), _maxw(0) {
	}

	virtual void initDimensions(const HistoryItem *parent = 0) = 0;
	virtual int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0) = 0; // return new height

	int32 height() const {
		return _height;
	}
	int32 maxWidth() const {
		return _maxw;
	}

	virtual ~HistoryElem() {
	}

protected:

	mutable int32 _height, _maxw, _minh;

};

class ItemAnimations : public Animated {
public:

	bool animStep(float64 ms);
	uint64 animate(const HistoryItem *item, uint64 ms);
	void remove(const HistoryItem *item);

private:
	typedef QMap<const HistoryItem*, uint64> Animations;
	Animations _animations;
};

ItemAnimations &itemAnimations();

class HistoryMedia;
class HistoryItem : public HistoryElem {
public:

	HistoryItem(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime msgDate, int32 from);

	enum {
		MsgType = 0,
		DateType,
		UnreadBarType
	};

	virtual void draw(QPainter &p, uint32 selection) const = 0;
	History *history() {
		return _history;
	}
	const History *history() const {
		return _history;
	}
	UserData *from() {
		return _from;
	}
	const UserData *from() const {
		return _from;
	}
	HistoryBlock *block() {
		return _block;
	}
	const HistoryBlock *block() const {
		return _block;
	}
	void destroy();
	void detach();
	void detachFast();
	bool detached() const {
		return !_block;
	}
	bool out() const {
		return _out;
	}
	bool unread() const {
		if ((_out && (id > 0 && id < _history->outboxReadTill)) || (!_out && id > 0 && id < _history->inboxReadTill)) return false;
		return _unread;
	}
	virtual bool needCheck() const {
		return true;
	}
	virtual bool hasPoint(int32 x, int32 y) const {
		return false;
	}
	virtual void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const {
		lnk = TextLinkPtr();
		inText = false;
	}
	virtual void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const { // from text
		upon = hasPoint(x, y);
		symbol = upon ? 0xFFFF : 0;
		after = false;
	}
	virtual uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return (from << 16) | to;
	}
	virtual int32 itemType() const {
		return MsgType;
	}
	virtual bool serviceMsg() const {
		return false;
	}
	virtual void updateMedia(const MTPMessageMedia &media) {
	}

	virtual QString selectedText(uint32 selection) const {
		return qsl("[-]");
	}

	virtual void drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const = 0;
    virtual QString notificationHeader() const {
        return QString();
    }
    virtual QString notificationText() const = 0;
	void markRead();

	int32 y, id;
	QDateTime date;

	virtual HistoryMedia *getMedia(bool inOverview = false) const {
		return 0;
	}
	virtual QString time() const {
		return QString();
	}
	virtual int32 timeWidth() const {
		return 0;
	}
	virtual bool animating() const {
		return false;
	}

	virtual ~HistoryItem();

protected:

	UserData *_from;
	mutable int32 _fromVersion;
	History *_history;
	HistoryBlock *_block;
	bool _out, _unread;

};

HistoryItem *regItem(HistoryItem *item, bool returnExisting = false);

class HistoryMedia : public HistoryElem {
public:

	HistoryMedia(int32 width = 0) : w(width) {
	}

	virtual HistoryMediaType type() const = 0;
	virtual const QString inDialogsText() const = 0;
	virtual bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const = 0;
	virtual int32 countHeight(const HistoryItem *parent, int32 width = -1) const {
		return height();
	}
	virtual int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0) {
		w = qMin(width, _maxw);
		return _height;
	}
	virtual TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const = 0;
	virtual void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const = 0;
	virtual bool uploading() const {
		return false;
	}
	virtual HistoryMedia *clone() const = 0;

	virtual void regItem(HistoryItem *item) {
	}

	virtual void unregItem(HistoryItem *item) {
	}

	virtual void updateFrom(const MTPMessageMedia &media) {
	}
	
	virtual bool animating() const {
		return false;
	}

	int32 currentWidth() const {
		return w;
	}

protected:

	int32 w;

};

class HistoryPhoto : public HistoryMedia {
public:

	HistoryPhoto(const MTPDphoto &photo, int32 width = 0);
	HistoryPhoto(PeerData *chat, const MTPDphoto &photo, int32 width = 0);

	void init();
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypePhoto;
	}
	const QString inDialogsText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	PhotoData *photo() const {
		return data;
	}

	TextLinkPtr lnk() const {
		return openl;
	}

	virtual bool animating() const {
		if (data->full->loaded()) return false;
		return data->full->loading() ? true : !data->medium->loaded();
	}

private:
	PhotoData *data;
	TextLinkPtr openl;

};

QString formatSizeText(qint64 size);

class HistoryVideo : public HistoryMedia {
public:

	HistoryVideo(const MTPDvideo &video, int32 width = 0);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	HistoryMediaType type() const {
		return MediaTypeVideo;
	}
	const QString inDialogsText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	bool uploading() const {
		return (data->status == FileUploading);
	}
	HistoryMedia *clone() const;

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

private:
	VideoData *data;
	TextLinkPtr _openl, _savel, _cancell;
	
	QString _size;
	int32 _thumbw, _thumbx, _thumby;

	mutable QString _dldTextCache, _uplTextCache;
	mutable int32 _dldDone, _uplDone;
};

class HistoryAudio : public HistoryMedia {
public:

	HistoryAudio(const MTPDaudio &audio, int32 width = 0);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	HistoryMediaType type() const {
		return MediaTypeAudio;
	}
	const QString inDialogsText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	bool uploading() const {
		return (data->status == FileUploading);
	}
	HistoryMedia *clone() const;

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

private:
	AudioData *data;
	TextLinkPtr _openl, _savel, _cancell;

	QString _size;

	mutable QString _dldTextCache, _uplTextCache;
	mutable int32 _dldDone, _uplDone;
};

class HistoryDocument : public HistoryMedia {
public:

	HistoryDocument(const MTPDdocument &document, int32 width = 0);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeDocument;
	}
	const QString inDialogsText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	int32 countHeight(const HistoryItem *parent, int32 width = -1) const;
	bool uploading() const {
		return (data->status == FileUploading);
	}
	TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	DocumentData *document() {
		return data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media);

private:

	DocumentData *data;
	TextLinkPtr _openl, _savel, _cancell;

	int32 _namew;
	QString _name, _size;
	int32 _thumbw, _thumbx, _thumby;

	mutable QString _dldTextCache, _uplTextCache;
	mutable int32 _dldDone, _uplDone;
};

class HistoryContact : public HistoryMedia {
public:

	HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeContact;
	}
	const QString inDialogsText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const;
	TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width) const;
	HistoryMedia *clone() const;

	void updateFrom(const MTPMessageMedia &media);

private:
	int32 userId;
	int32 w, phonew;
	Text name;
	QString phone;
	UserData *contact;
};

void initImageLinkManager();
void reinitImageLinkManager();
void deinitImageLinkManager();

enum ImageLinkType {
	InvalidImageLink = 0,
	YouTubeLink,
	InstagramLink,
	GoogleMapsLink
};
struct ImageLinkData {
	ImageLinkData(const QString &id) : id(id), type(InvalidImageLink), loading(false) {
	}

	QString id;
	QString title, duration;
	ImagePtr thumb;
	TextLinkPtr openl;
	ImageLinkType type;
	bool loading;

	void load();
};

class ImageLinkManager : public QObject {
	Q_OBJECT
public:
	ImageLinkManager() : manager(0), black(0) {
	}
	void init();
	void reinit();
	void deinit();

	void getData(ImageLinkData *data);

	~ImageLinkManager() {
		deinit();
	}

public slots:
	void onFinished(QNetworkReply *reply);
	void onFailed(QNetworkReply *reply);

private:
	void failed(ImageLinkData *data);

	QNetworkAccessManager *manager;
	QMap<QNetworkReply*, ImageLinkData*> dataLoadings, imageLoadings;
	QMap<ImageLinkData*, int32> serverRedirects;
	ImagePtr *black;
};

class HistoryImageLink : public HistoryMedia {
public:

	HistoryImageLink(const QString &url, int32 width = 0);
	int32 fullWidth() const;
	int32 fullHeight() const;
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeImageLink;
	}
	const QString inDialogsText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	TextLinkPtr getLink(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

private:
	ImageLinkData *data;

};

class HistoryMessage : public HistoryItem {
public:

	HistoryMessage(History *history, HistoryBlock *block, const MTPDmessage &msg);
//	HistoryMessage(History *history, HistoryBlock *block, const MTPDgeoChatMessage &msg);
//	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime date, int32 from, const QString &msg);
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime date, int32 from, const QString &msg, const MTPMessageMedia &media);
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, bool out, bool unread, QDateTime date, int32 from, const QString &msg, HistoryMedia *media);

	void initMedia(const MTPMessageMedia &media, QString &currentText);
	void initDimensions(const HistoryItem *parent = 0);
	void initDimensions(const QString &text);
	void fromNameUpdated() const;

	bool uploading() const;

	void draw(QPainter &p, uint32 selection) const;
	virtual void drawMessageText(QPainter &p, const QRect &trect, uint32 selection) const;

	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return _text.adjustSelection(from, to, type);
	}

	void drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
    QString notificationHeader() const;
    QString notificationText() const;
    
	void updateMedia(const MTPMessageMedia &media) {
		if (_media) _media->updateFrom(media);
	}

	QString selectedText(uint32 selection) const;
	HistoryMedia *getMedia(bool inOverview = false) const;

	QString time() const {
		return _time;
	}
	int32 timeWidth() const {
		return _timeWidth;
	}
	virtual bool animating() const {
		return _media ? _media->animating() : false;
	}

	~HistoryMessage();

protected:

	Text _text;

	int32 _textWidth, _textHeight;

	HistoryMedia *_media;
	QString _time;
	int32 _timeWidth;

};

class HistoryForwarded : public HistoryMessage {
public:

	HistoryForwarded(History *history, HistoryBlock *block, const MTPDmessageForwarded &msg);
	HistoryForwarded(History *history, HistoryBlock *block, MsgId id, HistoryMessage *msg);

	void fwdNameUpdated() const;

	void draw(QPainter &p, uint32 selection) const;
	void drawMessageText(QPainter &p, const QRect &trect, uint32 selection) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;

	QDateTime dateForwarded() const {
		return fwdDate;
	}
	UserData *fromForwarded() const {
		return fwdFrom;
	}
	QString selectedText(uint32 selection) const;

protected:

	QDateTime fwdDate;
	UserData *fwdFrom;
	mutable Text fwdFromName;
	mutable int32 fwdFromVersion;
	int32 fromWidth;

};

class HistoryServiceMsg : public HistoryItem {
public:

	HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDmessageService &msg);
//	HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDgeoChatMessageService &msg);
	HistoryServiceMsg(History *history, HistoryBlock *block, MsgId msgId, QDateTime date, const QString &msg, bool out = false, bool unread = false, HistoryMedia *media = 0);

	void initDimensions(const HistoryItem *parent = 0);

	void draw(QPainter &p, uint32 selection) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return _text.adjustSelection(from, to, type);
	}

	void drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
    QString notificationText() const;

	bool needCheck() const {
		return false;
	}
	bool serviceMsg() const {
		return true;
	}
	QString selectedText(uint32 selection) const;

	HistoryMedia *getMedia(bool inOverview = false) const;

	virtual bool animating() const {
		return _media ? _media->animating() : false;
	}

	~HistoryServiceMsg();

protected:

	QString messageByAction(const MTPmessageAction &action, TextLinkPtr &second);

	Text _text;
	HistoryMedia *_media;

	int32 _textWidth, _textHeight;
};

class HistoryDateMsg : public HistoryServiceMsg {
public:

	HistoryDateMsg(History *history, HistoryBlock *block, const QDate &date);
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const {
		lnk = TextLinkPtr();
		inText = false;
	}
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
		symbol = 0xFFFF;
		after = false;
		upon = false;
	}
	QString selectedText(uint32 selection) const {
		return QString();
	}
	int32 itemType() const {
		return DateType;
	}
};

HistoryItem *createDayServiceMsg(History *history, HistoryBlock *block, QDateTime date);

class HistoryUnreadBar : public HistoryItem {
public:

	HistoryUnreadBar(History *history, HistoryBlock *block, int32 count, const QDateTime &date);

	void initDimensions(const HistoryItem *parent = 0);

	void setCount(int32 count);

	void draw(QPainter &p, uint32 selection) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);

	void drawInDialog(QPainter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
    QString notificationText() const;

	QString selectedText(uint32 selection) const {
		return QString();
	}
	int32 itemType() const {
		return UnreadBarType;
	}

protected:

	QString text;
	bool freezed;
};
