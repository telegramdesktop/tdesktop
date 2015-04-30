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

void historyInit();

class HistoryItem;

void startGif(HistoryItem *row, const QString &file);
void itemRemovedGif(HistoryItem *item);
void itemReplacedGif(HistoryItem *oldItem, HistoryItem *newItem);
void stopGif();

static const uint32 FullItemSel = 0xFFFFFFFF;

typedef QMap<int32, HistoryItem*> SelectedItemSet;

extern TextParseOptions _textNameOptions, _textDlgOptions;

#include "structs.h"


struct History;
struct Histories : public QHash<PeerId, History*>, public Animated {
	typedef QHash<PeerId, History*> Parent;

	Histories() : unreadFull(0), unreadMuted(0) {
	}

	void regTyping(History *history, UserData *user);
	bool animStep(float64 ms);

	void clear();
	Parent::iterator erase(Parent::iterator i);
	void remove(const PeerId &peer);
	~Histories() {
		clear();

		unreadFull = unreadMuted = 0;
	}

	HistoryItem *addToBack(const MTPmessage &msg, int msgState = 1); // 2 - new read message, 1 - new unread message, 0 - not new message, -1 - searched message
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
	MediaTypeSticker,
	MediaTypeImageLink,
	MediaTypeWebPage,

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
		//	case MediaTypeSticker: return OverviewDocuments;
	case MediaTypeAudio: return OverviewAudios;
	}
	return OverviewCount;
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
	HistoryItem *createItemDocument(HistoryBlock *block, MsgId id, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc);

	HistoryItem *addToBackService(MsgId msgId, QDateTime date, const QString &text, int32 flags = 0, HistoryMedia *media = 0, bool newMsg = true);
	HistoryItem *addToBack(const MTPmessage &msg, bool newMsg = true);
	HistoryItem *addToHistory(const MTPmessage &msg);
	HistoryItem *addToBackForwarded(MsgId id, HistoryMessage *item);
	HistoryItem *addToBackDocument(MsgId id, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc);

	void addToFront(const QVector<MTPMessage> &slice);
	void addToBack(const QVector<MTPMessage> &slice);
	void createInitialDateBlock(const QDateTime &date);
	HistoryItem *doAddToBack(HistoryBlock *to, bool newBlock, HistoryItem *adding, bool newMsg);

	void newItemAdded(HistoryItem *item);
	void unregTyping(UserData *from);

	void inboxRead(int32 upTo);
	void inboxRead(HistoryItem *wasRead);
	void outboxRead(int32 upTo);
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
	bool hasNotification() const {
		return !notifies.isEmpty();
	}
	void skipNotification() {
		if (!notifies.isEmpty()) {
			notifies.pop_front();
		}
	}
	void popNotification(HistoryItem *item) {
		if (!notifies.isEmpty() && notifies.back() == item) notifies.pop_back();
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
	MsgId draftToId;
	MessageCursor draftCursor;
	bool draftPreviewCancelled;
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
	int32 minHeight() const {
		return _minh;
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

class HistoryReply; // dynamic_cast optimize
class HistoryMessage; // dynamic_cast optimize
class HistoryForwarded; // dynamic_cast optimize

class HistoryMedia;
class HistoryItem : public HistoryElem {
public:

	HistoryItem(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime msgDate, int32 from);

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
	UserData *from() const {
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
	void attach(HistoryBlock *block) {
		_block = block;
	}
	bool out() const {
		return _flags & MTPDmessage_flag_out;
	}
	bool unread() const {
		if ((out() && (id > 0 && id <= _history->outboxReadTill)) || (!out() && id > 0 && id <= _history->inboxReadTill)) return false;
		return _flags & MTPDmessage_flag_unread;
	}
	bool notifyByFrom() const {
		return _flags & MTPDmessage_flag_notify_by_from;
	}
	bool isMediaUnread() const {
		return _flags & MTPDmessage_flag_media_unread;
	}
	void markMediaRead() {
		_flags &= ~MTPDmessage_flag_media_unread;
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
	virtual void updateStickerEmoji() {
	}

	virtual QString selectedText(uint32 selection) const {
		return qsl("[-]");
	}
	virtual QString inDialogsText() const {
		return qsl("-");
	}
	virtual QString inReplyText() const {
		return inDialogsText();
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
	virtual void setMedia(const MTPmessageMedia &media) {
	}
	virtual QString time() const {
		return QString();
	}
	virtual int32 timeWidth(bool forText) const {
		return 0;
	}
	virtual bool animating() const {
		return false;
	}

	virtual HistoryMessage *toHistoryMessage() { // dynamic_cast optimize
		return 0;
	}
	virtual const HistoryMessage *toHistoryMessage() const { // dynamic_cast optimize
		return 0;
	}
	virtual HistoryForwarded *toHistoryForwarded() { // dynamic_cast optimize
		return 0;
	}
	virtual const HistoryForwarded *toHistoryForwarded() const { // dynamic_cast optimize
		return 0;
	}
	virtual HistoryReply *toHistoryReply() { // dynamic_cast optimize
		return 0;
	}
	virtual const HistoryReply *toHistoryReply() const { // dynamic_cast optimize
		return 0;
	}

	virtual ~HistoryItem();

protected:

	UserData *_from;
	mutable int32 _fromVersion;
	History *_history;
	HistoryBlock *_block;
	int32 _flags;

};

class MessageLink : public ITextLink {
public:
	MessageLink(PeerId peer, MsgId msgid) : _peer(peer), _msgid(msgid) {
	}
	void onClick(Qt::MouseButton button) const;
	PeerId peer() const {
		return _peer;
	}
	MsgId msgid() const {
		return _msgid;
	}

private:
	PeerId _peer;
	MsgId _msgid;
};

HistoryItem *regItem(HistoryItem *item, bool returnExisting = false);

class HistoryMedia : public HistoryElem {
public:

	HistoryMedia(int32 width = 0) : w(width) {
	}
	HistoryMedia(const HistoryMedia &other) : w(0) {
	}

	virtual HistoryMediaType type() const = 0;
	virtual const QString inDialogsText() const = 0;
	virtual const QString inHistoryText() const = 0;
	virtual bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const = 0;
	virtual int32 countHeight(const HistoryItem *parent, int32 width = -1) const {
		return height();
	}
	virtual int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0) {
		w = qMin(width, _maxw);
		return _height;
	}
	virtual void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const = 0;
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

	virtual bool isImageLink() const {
		return false;
	}

	virtual bool updateStickerEmoji() {
		return false;
	}
	
	virtual bool animating() const {
		return false;
	}

	virtual bool hasReplyPreview() const {
		return false;
	}
	virtual ImagePtr replyPreview() {
		return ImagePtr();
	}

	int32 currentWidth() const {
		return qMin(w, _maxw);
	}

protected:

	int32 w;

};

class HistoryPhoto : public HistoryMedia {
public:

	HistoryPhoto(const MTPDphoto &photo, const QString &caption, HistoryItem *parent);
	HistoryPhoto(PeerData *chat, const MTPDphoto &photo, int32 width = 0);

	void init();
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypePhoto;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	PhotoData *photo() const {
		return data;
	}

	void updateFrom(const MTPMessageMedia &media);

	TextLinkPtr lnk() const {
		return openl;
	}

	virtual bool animating() const {
		if (data->full->loaded()) return false;
		return data->full->loading() ? true : !data->medium->loaded();
	}

	bool hasReplyPreview() const {
		return !data->thumb->isNull();
	}
	ImagePtr replyPreview();

private:
	int16 pixw, pixh;
	PhotoData *data;
	Text _caption;
	TextLinkPtr openl;

};

QString formatSizeText(qint64 size);

class HistoryVideo : public HistoryMedia {
public:

	HistoryVideo(const MTPDvideo &video, const QString &caption, HistoryItem *parent);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	HistoryMediaType type() const {
		return MediaTypeVideo;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	bool uploading() const {
		return (data->status == FileUploading);
	}
	HistoryMedia *clone() const;

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	bool hasReplyPreview() const {
		return !data->thumb->isNull();
	}
	ImagePtr replyPreview();

private:
	VideoData *data;
	TextLinkPtr _openl, _savel, _cancell;
	
	Text _caption;

	QString _size;
	int32 _thumbw, _thumbx, _thumby;

	mutable QString _dldTextCache, _uplTextCache;
	mutable int32 _dldDone, _uplDone;
};

class HistoryAudio : public HistoryMedia {
public:

	HistoryAudio(const MTPDaudio &audio);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	HistoryMediaType type() const {
		return MediaTypeAudio;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
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

	HistoryDocument(DocumentData *document);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeDocument;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	int32 countHeight(const HistoryItem *parent, int32 width = -1) const;
	bool uploading() const {
		return (data->status == FileUploading);
	}
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	DocumentData *document() {
		return data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media);

	bool hasReplyPreview() const {
		return !data->thumb->isNull();
	}
	ImagePtr replyPreview();

private:

	DocumentData *data;
	TextLinkPtr _openl, _savel, _cancell;

	int32 _namew;
	QString _name, _size;
	int32 _thumbw, _thumbx, _thumby;

	mutable QString _dldTextCache, _uplTextCache;
	mutable int32 _dldDone, _uplDone;
};

class HistorySticker : public HistoryMedia {
public:

	HistorySticker(DocumentData *document);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeSticker;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	int32 countHeight(const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	DocumentData *document() {
		return data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media);
	bool updateStickerEmoji();

private:

	int16 pixw, pixh;
	DocumentData *data;
	QString _emoji;
	int32 lastw;

};

class HistoryContact : public HistoryMedia {
public:

	HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width) const;
	HistoryMediaType type() const {
		return MediaTypeContact;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width) const;
	HistoryMedia *clone() const;

	void updateFrom(const MTPMessageMedia &media);

private:
	int32 userId;
	int32 phonew;
	Text name;
	QString phone;
	UserData *contact;
};

class HistoryWebPage : public HistoryMedia {
public:

	HistoryWebPage(WebPageData *data);
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeWebPage;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	bool hasReplyPreview() const {
		return data->photo && !data->photo->thumb->isNull();
	}
	ImagePtr replyPreview();

private:
	WebPageData *data;
	TextLinkPtr _openl, _photol;
	bool _asArticle;

	Text _title, _description;
	int32 _siteNameWidth;

	QString _duration;
	int32 _durationWidth;

	int16 _pixw, _pixh;
};

void initImageLinkManager();
void reinitImageLinkManager();
void deinitImageLinkManager();

enum ImageLinkType {
	InvalidImageLink = 0,
	YouTubeLink,
	VimeoLink,
	InstagramLink,
	GoogleMapsLink
};
struct ImageLinkData {
	ImageLinkData(const QString &id) : id(id), type(InvalidImageLink), loading(false) {
	}

	QString id;
	QString title, duration;
	ImagePtr thumb;
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

	HistoryImageLink(const QString &url, const QString &title = QString(), const QString &description = QString());
	int32 fullWidth() const;
	int32 fullHeight() const;
	void initDimensions(const HistoryItem *parent);

	void draw(QPainter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	HistoryMediaType type() const {
		return MediaTypeImageLink;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	bool isImageLink() const {
		return true;
	}

private:
	ImageLinkData *data;
	Text _title, _description;
	TextLinkPtr link;

};

class HistoryMessage : public HistoryItem {
public:

	HistoryMessage(History *history, HistoryBlock *block, const MTPDmessage &msg);
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, const QString &msg, const MTPMessageMedia &media);
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, const QString &msg, HistoryMedia *media);
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, DocumentData *doc);

	void initTime();
	void initMedia(const MTPMessageMedia &media, QString &currentText);
	void initMediaFromText(QString &currentText);
	void initMediaFromDocument(DocumentData *doc);
	void initDimensions(const HistoryItem *parent = 0);
	void initDimensions(const QString &text);
	void fromNameUpdated() const;

	bool justMedia() const {
		return _media && _text.isEmpty();
	}

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
	void updateStickerEmoji();

	QString selectedText(uint32 selection) const;
	QString inDialogsText() const;
	HistoryMedia *getMedia(bool inOverview = false) const;
	void setMedia(const MTPmessageMedia &media);

	QString time() const {
		return _time;
	}
	int32 timeWidth(bool forText) const {
		return _timeWidth + (forText ? (st::msgDateSpace + (out() ? st::msgDateCheckSpace + st::msgCheckRect.pxWidth() : 0) - st::msgDateDelta.x()) : 0);
	}
	virtual bool animating() const {
		return _media ? _media->animating() : false;
	}

	virtual QDateTime dateForwarded() const { // dynamic_cast optimize
		return date;
	}
	virtual UserData *fromForwarded() const { // dynamic_cast optimize
		return from();
	}

	HistoryMessage *toHistoryMessage() { // dynamic_cast optimize
		return this;
	}
	const HistoryMessage *toHistoryMessage() const { // dynamic_cast optimize
		return this;
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

	HistoryForwarded(History *history, HistoryBlock *block, const MTPDmessage &msg);
	HistoryForwarded(History *history, HistoryBlock *block, MsgId id, HistoryMessage *msg);

	void initDimensions(const HistoryItem *parent = 0);
	void fwdNameUpdated() const;

	void draw(QPainter &p, uint32 selection) const;
	void drawForwardedFrom(QPainter &p, int32 x, int32 y, int32 w, bool selected) const;
	void drawMessageText(QPainter &p, const QRect &trect, uint32 selection) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const;
	void getForwardedState(TextLinkPtr &lnk, bool &inText, int32 x, int32 w) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;

	QDateTime dateForwarded() const {
		return fwdDate;
	}
	UserData *fromForwarded() const {
		return fwdFrom;
	}
	QString selectedText(uint32 selection) const;

	HistoryForwarded *toHistoryForwarded() {
		return this;
	}
	const HistoryForwarded *toHistoryForwarded() const {
		return this;
	}

protected:

	QDateTime fwdDate;
	UserData *fwdFrom;
	mutable Text fwdFromName;
	mutable int32 fwdFromVersion;
	int32 fromWidth;

};

class HistoryReply : public HistoryMessage {
public:

	HistoryReply(History *history, HistoryBlock *block, const MTPDmessage &msg);
	HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc);

	void initDimensions(const HistoryItem *parent = 0);

	bool updateReplyTo(bool force = false);
	void replyToNameUpdated() const;
	int32 replyToWidth() const;

	TextLinkPtr replyToLink() const;

	MsgId replyToId() const;
	HistoryItem *replyToMessage() const;
	void replyToReplaced(HistoryItem *oldItem, HistoryItem *newItem);

	void draw(QPainter &p, uint32 selection) const;
	void drawReplyTo(QPainter &p, int32 x, int32 y, int32 w, bool selected, bool likeService = false) const;
	void drawMessageText(QPainter &p, const QRect &trect, uint32 selection) const;
	int32 resize(int32 width, bool dontRecountText = false, const HistoryItem *parent = 0);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;

	UserData *replyTo() const {
		return replyToMsg ? replyToMsg->from() : 0;
	}
	QString selectedText(uint32 selection) const;

	HistoryReply *toHistoryReply() { // dynamic_cast optimize
		return this;
	}
	const HistoryReply *toHistoryReply() const { // dynamic_cast optimize
		return this;
	}

	~HistoryReply();

protected:

	MsgId replyToMsgId;
	HistoryItem *replyToMsg;
	TextLinkPtr replyToLnk;
	mutable Text replyToName, replyToText;
	mutable int32 replyToVersion;
	mutable int32 _maxReplyWidth;
	int32 toWidth;

};

class HistoryServiceMsg : public HistoryItem {
public:

	HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDmessageService &msg);
	HistoryServiceMsg(History *history, HistoryBlock *block, MsgId msgId, QDateTime date, const QString &msg, int32 flags = 0, HistoryMedia *media = 0);

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
	QString inDialogsText() const;
	QString inReplyText() const;

	HistoryMedia *getMedia(bool inOverview = false) const;

	virtual bool animating() const {
		return _media ? _media->animating() : false;
	}

	~HistoryServiceMsg();

protected:

	void setMessageByAction(const MTPmessageAction &action);

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
