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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
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

extern TextParseOptions _textNameOptions, _textDlgOptions, _historyTextOptions, _historyBotOptions;

#include "structs.h"

enum NewMessageType {
	NewMessageUnread,
	NewMessageLast,
	NewMessageExisting,
};

class History;
class Histories : public Animated {
public:
	typedef QHash<PeerId, History*> Map;
	Map map;

	Histories() : unreadFull(0), unreadMuted(0) {
	}

	void regSendAction(History *history, UserData *user, const MTPSendMessageAction &action);
	bool animStep(float64 ms);

	History *find(const PeerId &peerId);
	History *findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead);

	void clear();
	void remove(const PeerId &peer);
	~Histories() {
		clear();
		unreadFull = unreadMuted = 0;
	}

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	//	HistoryItem *addToBack(const MTPgeoChatMessage &msg, bool newMsg = true);

	typedef QMap<History*, uint64> TypingHistories; // when typing in this history started
	TypingHistories typing;

	int32 unreadFull, unreadMuted;
};

class HistoryBlock;

struct DialogRow {
	DialogRow(History *history = 0, DialogRow *prev = 0, DialogRow *next = 0, int32 pos = 0) : prev(prev), next(next), history(history), pos(pos), attached(0) {
	}

	void paint(Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const;

	DialogRow *prev, *next;
	History *history;
	int32 pos;
	void *attached; // for any attached data, for example View in contacts list
};

struct FakeDialogRow {
	FakeDialogRow(HistoryItem *item) : _item(item), _cacheFor(0), _cache(st::dlgRichMinWidth) {
	}

	void paint(Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const;

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
	OverviewAudioDocuments,
	OverviewLinks,

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
	case OverviewAudioDocuments: return MTP_inputMessagesFilterAudioDocuments();
	case OverviewLinks: return MTP_inputMessagesFilterUrl();
	default: type = OverviewCount; break;
	}
	return MTPMessagesFilter();
}

enum SendActionType {
	SendActionTyping,
	SendActionRecordVideo,
	SendActionUploadVideo,
	SendActionRecordAudio,
	SendActionUploadAudio,
	SendActionUploadPhoto,
	SendActionUploadFile,
	SendActionChooseLocation,
	SendActionChooseContact,
};
struct SendAction {
	SendAction(SendActionType type, uint64 until, int32 progress = 0) : type(type), until(until), progress(progress) {
	}
	SendActionType type;
	uint64 until;
	int32 progress;
};

class HistoryMedia;
class HistoryMessage;
class HistoryUnreadBar;

class ChannelHistory;
class History {
public:

	History(const PeerId &peerId);
	ChannelId channelId() const {
		return peerToChannel(peer->id);
	}
	bool isChannel() const {
		return peerIsChannel(peer->id);
	}
	ChannelHistory *asChannelHistory();
	const ChannelHistory *asChannelHistory() const;

	bool isEmpty() const {
		return blocks.isEmpty();
	}
	void clear(bool leaveItems = false);
	void blockResized(HistoryBlock *block, int32 dh);
	void removeBlock(HistoryBlock *block);

	virtual ~History() {
		clear();
	}

	HistoryItem *createItem(HistoryBlock *block, const MTPMessage &msg, bool applyServiceAction, bool returnExisting = false);
	HistoryItem *createItemForwarded(HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg);
	HistoryItem *createItemDocument(HistoryBlock *block, MsgId id, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc);

	HistoryItem *addNewService(MsgId msgId, QDateTime date, const QString &text, int32 flags = 0, HistoryMedia *media = 0, bool newMsg = true);
	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addToHistory(const MTPMessage &msg);
	HistoryItem *addNewForwarded(MsgId id, QDateTime date, int32 from, HistoryMessage *item);
	HistoryItem *addNewDocument(MsgId id, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc);

	void addOlderSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed);
	void addNewerSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed);
	void addToOverview(HistoryItem *item, MediaOverviewType type);
	bool addToOverviewFront(HistoryItem *item, MediaOverviewType type);

	void newItemAdded(HistoryItem *item);
	void unregTyping(UserData *from);

	int32 countUnread(MsgId upTo);
	void updateShowFrom();
	MsgId inboxRead(MsgId upTo);
	MsgId inboxRead(HistoryItem *wasRead);
	MsgId outboxRead(MsgId upTo);
	MsgId outboxRead(HistoryItem *wasRead);

	HistoryItem *lastImportantMessage() const;

	void setUnreadCount(int32 newUnreadCount, bool psUpdate = true);
	void setMute(bool newMute);
	void getNextShowFrom(HistoryBlock *block, int32 i);
	void addUnreadBar();
	void clearNotifications();

	bool loadedAtBottom() const; // last message is in the list
	void setNotLoadedAtBottom();
	bool loadedAtTop() const; // nothing was added after loading history back
	bool isReadyFor(MsgId msgId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop); // has messages for showing history at msgId
	void getReadyFor(MsgId msgId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop);

	void setLastMessage(HistoryItem *msg);
	void setPosInDialogsDate(const QDateTime &date);
	void fixLastMessage(bool wasAtBottom);

	MsgId minMsgId() const;
	MsgId maxMsgId() const;
	MsgId msgIdForRead() const;

	int32 geomResize(int32 newWidth, int32 *ytransform = 0, HistoryItem *resizedItem = 0); // return new size

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
		if (lastMsg == old) {
			lastMsg = item;
		}
		// showFrom can't be detached
	}

	void paintDialog(Painter &p, int32 w, bool sel) const;
	void eraseFromOverview(MediaOverviewType type, MsgId msgId);
	bool updateTyping(uint64 ms = 0, uint32 dots = 0, bool force = false);
	void clearLastKeyboard();

	typedef QList<HistoryBlock*> Blocks;
	Blocks blocks;

	int32 width, height, msgCount, unreadCount;
	int32 inboxReadBefore, outboxReadBefore;
	HistoryItem *showFrom;
	HistoryUnreadBar *unreadBar;

	PeerData *peer;
	bool oldLoaded, newLoaded;
	HistoryItem *lastMsg;
	QDateTime lastMsgDate;

	typedef QList<HistoryItem*> NotifyQueue;
	NotifyQueue notifies;

	QString draft;
	MsgId draftToId;
	MessageCursor draftCursor;
	bool draftPreviewCancelled;
	int32 lastWidth, lastScrollTop;
	MsgId lastShowAtMsgId;
	bool mute;

	bool lastKeyboardInited, lastKeyboardUsed;
	MsgId lastKeyboardId;
	PeerId lastKeyboardFrom;

	mtpRequestId sendRequestId;

	mutable const HistoryItem *textCachedFor; // cache
	mutable Text lastItemTextCache;

	typedef QMap<QChar, DialogRow*> DialogLinks;
	DialogLinks dialogs;
	uint64 posInDialogs; // like ((unixtime) << 32) | (incremented counter)

	typedef QMap<UserData*, uint64> TypingUsers;
	TypingUsers typing;
	typedef QMap<UserData*, SendAction> SendActionUsers;
	SendActionUsers sendActions;
	QString typingStr;
	Text typingText;
	uint32 typingFrame;
	QMap<SendActionType, uint64> mySendActions;

	typedef QList<MsgId> MediaOverview;
	typedef QMap<MsgId, NullType> MediaOverviewIds;
	MediaOverview overview[OverviewCount];
	MediaOverviewIds overviewIds[OverviewCount];
	int32 overviewCount[OverviewCount]; // -1 - not loaded, 0 - all loaded, > 0 - count, but not all loaded

private:

	friend class HistoryBlock;
	friend class ChannelHistory;

	void createInitialDateBlock(const QDateTime &date);
	HistoryItem *addItemAfterPrevToBlock(HistoryItem *item, HistoryItem *prev, HistoryBlock *block);
	HistoryItem *addNewInTheMiddle(HistoryItem *newItem, int32 blockIndex, int32 itemIndex);
	HistoryItem *addNewItem(HistoryBlock *to, bool newBlock, HistoryItem *adding, bool newMsg);
	HistoryItem *addMessageGroupAfterPrevToBlock(const MTPDmessageGroup &group, HistoryItem *prev, HistoryBlock *block);
	HistoryItem *addMessageGroupAfterPrev(HistoryItem *newItem, HistoryItem *prev);

};

class HistoryGroup;
class HistoryCollapse;
class HistoryJoined;
class ChannelHistory : public History {
public:

	ChannelHistory(const PeerId &peer);

	void messageDetached(HistoryItem *msg);
	void messageDeleted(HistoryItem *msg);
	void messageWithIdDeleted(MsgId msgId);

	bool isSwitchReadyFor(MsgId switchId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop); // has messages for showing history after switching mode at switchId
	void getSwitchReadyFor(MsgId switchId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop);

	void insertCollapseItem(MsgId wasMinId);
	void getRangeDifference();
	void getRangeDifferenceNext(int32 pts);

	void addNewGroup(const MTPMessageGroup &group);

	int32 unreadCountAll;
	bool onlyImportant() const {
		return _onlyImportant;
	}

	HistoryCollapse *collapse() const {
		return _collapseMessage;
	}

	void clearOther() {
		_otherNewLoaded = true;
		_otherOldLoaded = false;
		_otherList.clear();
	}

	HistoryJoined *insertJoinedMessage(bool unread);
	void checkJoinedMessage(bool createUnread = false);
	const QDateTime &maxReadMessageDate();

private:

	friend class History;
	HistoryItem* addNewChannelMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addNewToBlocks(const MTPMessage &msg, NewMessageType type);
	void addNewToOther(HistoryItem *item, NewMessageType type);

	void checkMaxReadMessageDate();

	HistoryGroup *findGroup(MsgId msgId) const;
	HistoryBlock *findGroupBlock(MsgId msgId) const;
	HistoryGroup *findGroupInOther(MsgId msgId) const;
	HistoryItem *findPrevItem(HistoryItem *item) const;
	void switchMode();

	void cleared();

	bool _onlyImportant;

	QDateTime _maxReadMessageDate;

	typedef QList<HistoryItem*> OtherList;
	OtherList _otherList;
	bool _otherOldLoaded, _otherNewLoaded;

	HistoryCollapse *_collapseMessage;
	HistoryJoined *_joinedMessage;

	MsgId _rangeDifferenceFromId, _rangeDifferenceToId;
	int32 _rangeDifferencePts;
	mtpRequestId _rangeDifferenceRequestId;

};

enum DialogsSortMode {
	DialogsSortByDate,
	DialogsSortByName,
	DialogsSortByAdd
};

struct DialogsList {
	DialogsList(DialogsSortMode sortMode) : begin(&last), end(&last), sortMode(sortMode), count(0), current(&last) {
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

	void paint(Painter &p, int32 w, int32 hFrom, int32 hTo, PeerData *act, PeerData *sel, bool onlyBackground) const {
		adjustCurrent(hFrom, st::dlgHeight);

		DialogRow *drawFrom = current;
		p.translate(0, drawFrom->pos * st::dlgHeight);
		while (drawFrom != end && drawFrom->pos * st::dlgHeight < hTo) {
			drawFrom->paint(p, w, (drawFrom->history->peer == act), (drawFrom->history->peer == sel), onlyBackground);
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

	DialogRow *addToEnd(History *history) {
		DialogRow *result = new DialogRow(history, end->prev, end, end->pos);
		end->pos++;
		if (begin == end) {
			begin = current = result;
		} else {
			end->prev->next = result;
		}
		rowByPeer.insert(history->peer->id, result);
		++count;
		end->prev = result;
		if (sortMode == DialogsSortByDate) {
			adjustByPos(result);
		}
		return result;
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
		if (sortMode != DialogsSortByName) return 0;

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
		if (sortMode != DialogsSortByName) return 0;

		DialogRow *row = addToEnd(history), *change = row;
		const QString &peerName(history->peer->name);
		while (change->prev && change->prev->history->peer->name.compare(peerName, Qt::CaseInsensitive) > 0) {
			change = change->prev;
		}
		if (!insertBefore(row, change)) {
			while (change->next != end && change->next->history->peer->name.compare(peerName, Qt::CaseInsensitive) < 0) {
				change = change->next;
			}
			insertAfter(row, change);
		}
		return row;
	}

	void adjustByPos(DialogRow *row) {
		if (sortMode != DialogsSortByDate) return;

		DialogRow *change = row;
		if (change != begin && begin->history->posInDialogs < row->history->posInDialogs) {
			change = begin;
		} else while (change->prev && change->prev->history->posInDialogs < row->history->posInDialogs) {
			change = change->prev;
		}
		if (!insertBefore(row, change)) {
			if (change->next != end && end->prev->history->posInDialogs > row->history->posInDialogs) {
				change = end->prev;
			} else while (change->next != end && change->next->history->posInDialogs > row->history->posInDialogs) {
				change = change->next;
			}
			insertAfter(row, change);
		}
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
	DialogsSortMode sortMode;
	int32 count;

	typedef QHash<PeerId, DialogRow*> RowByPeer;
	RowByPeer rowByPeer;

	mutable DialogRow *current; // cache
};

struct DialogsIndexed {
	DialogsIndexed(DialogsSortMode sortMode) : sortMode(sortMode), list(sortMode) {
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
				j = index.insert(*i, new DialogsList(sortMode));
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
				j = index.insert(*i, new DialogsList(sortMode));
			}
			j.value()->addByName(history);
		}
		return res;
	}

	void adjustByPos(const History::DialogLinks &links) {
		for (History::DialogLinks::const_iterator i = links.cbegin(), e = links.cend(); i != e; ++i) {
			if (i.key() == QChar(0)) {
				list.adjustByPos(i.value());
			} else {
				DialogsIndex::iterator j = index.find(i.key());
				if (j != index.cend()) {
					j.value()->adjustByPos(i.value());
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

	DialogsSortMode sortMode;
	DialogsList list;
	typedef QMap<QChar, DialogsList*> DialogsIndex;
	DialogsIndex index;
};

class HistoryBlock {
public:
	HistoryBlock(History *hist) : y(0), height(0), history(hist) {
	}

	typedef QVector<HistoryItem*> Items;
	Items items;

	void clear(bool leaveItems = false);
	~HistoryBlock() {
		clear();
	}
	void removeItem(HistoryItem *item);

	int32 geomResize(int32 newWidth, int32 *ytransform, HistoryItem *resizedItem); // return new size
	int32 y, height;
	History *history;
};

class HistoryElem {
public:

	HistoryElem() : _height(0), _maxw(0) {
	}

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

enum HistoryCursorState {
	HistoryDefaultCursorState,
	HistoryInTextCursorState,
	HistoryInDateCursorState
};

enum InfoDisplayType {
	InfoDisplayDefault,
	InfoDisplayOverImage,
};

inline bool isImportantChannelMessage(MsgId id, int32 flags) { // client-side important msgs always has_views or has_from_id
	return (flags & MTPDmessage_flag_out) || (flags & MTPDmessage_flag_notify_by_from) || ((id > 0 || flags != 0) && !(flags & MTPDmessage::flag_from_id));
}

enum HistoryItemType {
	HistoryItemMsg = 0,
	HistoryItemDate,
	HistoryItemUnreadBar,
	HistoryItemGroup,
	HistoryItemCollapse,
	HistoryItemJoined
};

class HistoryMedia;
class HistoryItem : public HistoryElem {
public:

	HistoryItem(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime msgDate, int32 from);

	virtual void initDimensions() = 0;
	virtual int32 resize(int32 width) = 0; // return new height
	virtual void draw(Painter &p, uint32 selection) const = 0;

	History *history() {
		return _history;
	}
	const History *history() const {
		return _history;
	}
	PeerData *from() const {
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
		if ((out() && (id > 0 && id < _history->outboxReadBefore)) || (!out() && id > 0 && id < _history->inboxReadBefore)) return false;
		return (id > 0 && !out() && channelId() != NoChannel) ? true : (_flags & MTPDmessage_flag_unread);
	}
	bool notifyByFrom() const {
		return _flags & MTPDmessage_flag_notify_by_from;
	}
	bool isMediaUnread() const {
		return (_flags & MTPDmessage_flag_media_unread) && (channelId() == NoChannel);
	}
	void markMediaRead() {
		_flags &= ~MTPDmessage_flag_media_unread;
	}
	bool hasReplyMarkup() const {
		return _flags & MTPDmessage::flag_reply_markup;
	}
	bool hasTextLinks() const {
		return _flags & MTPDmessage_flag_HAS_TEXT_LINKS;
	}
	bool hasViews() const {
		return _flags & MTPDmessage::flag_views;
	}
	bool fromChannel() const {
		return _from->isChannel();
	}
	bool isImportant() const {
		return _history->isChannel() && isImportantChannelMessage(id, _flags);
	}
	virtual bool needCheck() const {
		return out();
	}
	virtual bool hasPoint(int32 x, int32 y) const {
		return false;
	}
	virtual void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
		lnk = TextLinkPtr();
		state = HistoryDefaultCursorState;
	}
	virtual void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const { // from text
		upon = hasPoint(x, y);
		symbol = upon ? 0xFFFF : 0;
		after = false;
	}
	virtual uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return (from << 16) | to;
	}
	virtual HistoryItemType type() const {
		return HistoryItemMsg;
	}
	virtual bool serviceMsg() const {
		return false;
	}
	virtual void updateMedia(const MTPMessageMedia *media, bool allowEmitResize) {
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

	virtual void drawInfo(Painter &p, int32 right, int32 bottom, bool selected, InfoDisplayType type) const {
	}
	virtual void setViewsCount(int32 count) {
	}
	virtual void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const = 0;
    virtual QString notificationHeader() const {
        return QString();
    }
    virtual QString notificationText() const = 0;

	bool canDelete() const {
		ChannelData *channel = _history->peer->asChannel();
		if (!channel) return true;

		if (id == 1) return false;
		if (channel->amCreator()) return true;
		if (fromChannel()) {
			if (channel->amEditor() && out()) return true;
			return false;
		}
		return (channel->amEditor() || channel->amModerator() || out());
	}

	int32 y;
	MsgId id;
	QDateTime date;

	ChannelId channelId() const {
		return _history->channelId();
	}
	FullMsgId fullId() const {
		return FullMsgId(channelId(), id);
	}

	virtual HistoryMedia *getMedia(bool inOverview = false) const {
		return 0;
	}
	virtual void setText(const QString &text, const LinksInText &links) {
	}
	virtual void getTextWithLinks(QString &text, LinksInText &links) {
	}
	virtual bool textHasLinks() {
		return false;
	}

	virtual int32 infoWidth() const {
		return 0;
	}
	virtual int32 timeLeft() const {
		return 0;
	}
	virtual QString timeText() const {
		return QString();
	}
	virtual int32 timeWidth() const {
		return 0;
	}
	virtual QString viewsText() const {
		return QString();
	}
	virtual int32 viewsWidth() const {
		return 0;
	}
	virtual bool pointInTime(int32 right, int32 bottom, int32 x, int32 y, InfoDisplayType type) const {
		return false;
	}

	int32 skipBlockWidth() const {
		return st::msgDateSpace + infoWidth() - st::msgDateDelta.x();
	}
	int32 skipBlockHeight() const {
		return st::msgDateFont->height - st::msgDateDelta.y();
	}
	QString skipBlock() const {
		return textcmdSkipBlock(skipBlockWidth(), skipBlockHeight());
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

	bool displayFromName() const {
		return (!out() || fromChannel()) && !history()->peer->isUser();
	}
	bool displayFromPhoto() const {
		return !out() && !history()->peer->isUser() && !fromChannel();
	}

	virtual ~HistoryItem();

protected:

	PeerData *_from;
	mutable int32 _fromVersion;
	History *_history;
	HistoryBlock *_block;
	int32 _flags;

};

class MessageLink : public ITextLink {
	TEXT_LINK_CLASS(MessageLink)

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

class CommentsLink : public ITextLink {
	TEXT_LINK_CLASS(CommentsLink)

public:
	CommentsLink(HistoryItem *item) : _item(item) {
	}
	void onClick(Qt::MouseButton button) const;
	
private:
	HistoryItem *_item;
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
	virtual bool isDisplayed() const {
		return true;
	}
	virtual int32 countHeight(const HistoryItem *parent, int32 width = -1) const {
		return height();
	}
	virtual void initDimensions(const HistoryItem *parent) = 0;
	virtual int32 resize(int32 width, const HistoryItem *parent) { // return new height
		w = qMin(width, _maxw);
		return _height;
	}
	virtual void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const = 0;
	virtual void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const = 0;
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

	virtual bool animating() const {
		return false;
	}

	virtual bool hasReplyPreview() const {
		return false;
	}
	virtual ImagePtr replyPreview() {
		return ImagePtr();
	}
	virtual QString getCaption() const {
		return QString();
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

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, const HistoryItem *parent);
	HistoryMediaType type() const {
		return MediaTypePhoto;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	const Text &captionForClone() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
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

	QString getCaption() const {
		return _caption.original(0, 0xFFFFU, true);
	}

private:
	int16 pixw, pixh;
	PhotoData *data;
	Text _caption;
	TextLinkPtr openl;

};

QString formatSizeText(qint64 size);
QString formatDownloadText(qint64 ready, qint64 total);
QString formatDurationText(qint64 duration);

class HistoryVideo : public HistoryMedia {
public:

	HistoryVideo(const MTPDvideo &video, const QString &caption, HistoryItem *parent);
	void initDimensions(const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, const HistoryItem *parent);
	HistoryMediaType type() const {
		return MediaTypeVideo;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	int32 countHeight(const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
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
	int32 _thumbw;

	mutable QString _dldTextCache, _uplTextCache;
	mutable int32 _dldDone, _uplDone;
};

class HistoryAudio : public HistoryMedia {
public:

	HistoryAudio(const MTPDaudio &audio);
	void initDimensions(const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	HistoryMediaType type() const {
		return MediaTypeAudio;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	bool uploading() const {
		return (data->status == FileUploading);
	}
	HistoryMedia *clone() const;

	AudioData *audio() {
		return data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media);

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

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, const HistoryItem *parent);
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
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
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

	void drawInPlaylist(Painter &p, const HistoryItem *parent, bool selected, bool over, int32 width) const;
	TextLinkPtr linkInPlaylist();

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

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, const HistoryItem *parent);
	HistoryMediaType type() const {
		return MediaTypeSticker;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	int32 countHeight(const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	DocumentData *document() {
		return data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media);

private:

	int16 pixw, pixh;
	DocumentData *data;
	QString _emoji;
	int32 lastw;

};

class HistoryContact : public HistoryMedia {
public:

	HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone);
	HistoryContact(int32 userId, const QString &fullname, const QString &phone);
	void initDimensions(const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const;
	HistoryMediaType type() const {
		return MediaTypeContact;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const;
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

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	bool isDisplayed() const {
		return !data->pendingTill;
	}
	int32 resize(int32 width, const HistoryItem *parent);
	HistoryMediaType type() const {
		return MediaTypeWebPage;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	HistoryMedia *clone() const;

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	bool hasReplyPreview() const {
		return (data->photo && !data->photo->thumb->isNull()) || (data->doc && !data->doc->thumb->isNull());
	}
	ImagePtr replyPreview();

	virtual bool animating() const {
		if (_asArticle || !data->photo || data->photo->full->loaded()) return false;
		return data->photo->full->loading();
	}

	WebPageData *webpage() {
		return data;
	}

private:
	WebPageData *data;
	TextLinkPtr _openl, _attachl;
	bool _asArticle;

	Text _title, _description;
	int32 _siteNameWidth;

	QString _duration, _docName, _docSize;
	int32 _durationWidth, _docNameWidth, _docThumbWidth;
	mutable QString _docDownloadTextCache;
	mutable int32 _docDownloadDone;

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

	void draw(Painter &p, const HistoryItem *parent, bool selected, int32 width = -1) const;
	int32 resize(int32 width, const HistoryItem *parent);
	HistoryMediaType type() const {
		return MediaTypeImageLink;
	}
	const QString inDialogsText() const;
	const QString inHistoryText() const;
	bool hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width = -1) const;
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
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, const QString &msg, const LinksInText &links, HistoryMedia *media); // local forwarded
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, DocumentData *doc); // local sticker and reply sticker

	void initTime();
	void initMedia(const MTPMessageMedia *media, QString &currentText);
	void initMediaFromText(QString &currentText);
	void initMediaFromDocument(DocumentData *doc);
	void initDimensions();
	void fromNameUpdated() const;

	bool justMedia() const {
		return _media && _text.isEmpty();
	}

	bool uploading() const;

	void drawInfo(Painter &p, int32 right, int32 bottom, bool selected, InfoDisplayType type) const;
	void setViewsCount(int32 count);
	void draw(Painter &p, uint32 selection) const;
	virtual void drawMessageText(Painter &p, const QRect &trect, uint32 selection) const;

	int32 resize(int32 width);
	bool hasPoint(int32 x, int32 y) const;
	bool pointInTime(int32 right, int32 bottom, int32 x, int32 y, InfoDisplayType type) const;

	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	virtual void getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const;

	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return _text.adjustSelection(from, to, type);
	}

	void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
    QString notificationHeader() const;
    QString notificationText() const;
    
	void updateMedia(const MTPMessageMedia *media, bool allowEmitResize) {
		if (media && _media && _media->type() != MediaTypeWebPage) {
			_media->updateFrom(*media);
		} else {
			setMedia(media, allowEmitResize);
		}
	}

	QString selectedText(uint32 selection) const;
	LinksInText textLinks() const;
	QString inDialogsText() const;
	HistoryMedia *getMedia(bool inOverview = false) const;
	void setMedia(const MTPMessageMedia *media, bool allowEmitResize);
	void setText(const QString &text, const LinksInText &links);
	void getTextWithLinks(QString &text, LinksInText &links);
	bool textHasLinks();

	int32 infoWidth() const {
		int32 result = _timeWidth;
		if (!_viewsText.isEmpty()) {
			result += st::msgDateViewsSpace + _viewsWidth + st::msgDateCheckSpace + st::msgViewsImg.pxWidth();
		}
		if (out() && !fromChannel()) {
			result += st::msgDateCheckSpace + st::msgCheckImg.pxWidth();
		}
		return result;
	}
	int32 timeLeft() const {
		int32 result = 0;
		if (!_viewsText.isEmpty()) {
			result += st::msgDateViewsSpace + _viewsWidth + st::msgDateCheckSpace + st::msgViewsImg.pxWidth();
		}
		return result;
	}
	QString timeText() const {
		return _timeText;
	}
	int32 timeWidth() const {
		return _timeWidth;
	}
	QString viewsText() const {
		return _viewsText;
	}
	int32 viewsWidth() const {
		return _viewsWidth;
	}
	virtual bool animating() const {
		return _media ? _media->animating() : false;
	}

	virtual QDateTime dateForwarded() const { // dynamic_cast optimize
		return date;
	}
	virtual PeerData *fromForwarded() const { // dynamic_cast optimize
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
	QString _timeText;
	int32 _timeWidth;
	
	QString _viewsText;
	int32 _views, _viewsWidth;

};

class HistoryForwarded : public HistoryMessage {
public:

	HistoryForwarded(History *history, HistoryBlock *block, const MTPDmessage &msg);
	HistoryForwarded(History *history, HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg);

	void initDimensions();
	void fwdNameUpdated() const;

	void draw(Painter &p, uint32 selection) const;
	void drawForwardedFrom(Painter &p, int32 x, int32 y, int32 w, bool selected) const;
	void drawMessageText(Painter &p, const QRect &trect, uint32 selection) const;
	int32 resize(int32 width);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	void getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const;
	void getForwardedState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 w) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;

	QDateTime dateForwarded() const {
		return fwdDate;
	}
	PeerData *fromForwarded() const {
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
	PeerData *fwdFrom;
	mutable Text fwdFromName;
	mutable int32 fwdFromVersion;
	int32 fromWidth;

};

class HistoryReply : public HistoryMessage {
public:

	HistoryReply(History *history, HistoryBlock *block, const MTPDmessage &msg);
	HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc);

	void initDimensions();

	bool updateReplyTo(bool force = false);
	void replyToNameUpdated() const;
	int32 replyToWidth() const;

	TextLinkPtr replyToLink() const;

	MsgId replyToId() const;
	HistoryItem *replyToMessage() const;
	void replyToReplaced(HistoryItem *oldItem, HistoryItem *newItem);

	void draw(Painter &p, uint32 selection) const;
	void drawReplyTo(Painter &p, int32 x, int32 y, int32 w, bool selected, bool likeService = false) const;
	void drawMessageText(Painter &p, const QRect &trect, uint32 selection) const;
	int32 resize(int32 width);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	void getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;

	PeerData *replyTo() const {
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
	HistoryServiceMsg(History *history, HistoryBlock *block, MsgId msgId, QDateTime date, const QString &msg, int32 flags = 0, HistoryMedia *media = 0, int32 from = 0);

	void initDimensions();

	void draw(Painter &p, uint32 selection) const;
	int32 resize(int32 width);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return _text.adjustSelection(from, to, type);
	}

	void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
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

	void setServiceText(const QString &text);

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
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
		lnk = TextLinkPtr();
		state = HistoryDefaultCursorState;
	}
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
		symbol = 0xFFFF;
		after = false;
		upon = false;
	}
	QString selectedText(uint32 selection) const {
		return QString();
	}
	HistoryItemType type() const {
		return HistoryItemDate;
	}
};

class HistoryGroup : public HistoryServiceMsg {
public:

	HistoryGroup(History *history, HistoryBlock *block, const MTPDmessageGroup &group, const QDateTime &date);
	HistoryGroup(History *history, HistoryBlock *block, HistoryItem *newItem, const QDateTime &date);
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
		symbol = 0xFFFF;
		after = false;
		upon = false;
	}
	QString selectedText(uint32 selection) const {
		return QString();
	}
	HistoryItemType type() const {
		return HistoryItemGroup;
	}
	void uniteWith(MsgId minId, MsgId maxId, int32 count);
	void uniteWith(HistoryItem *item) {
		uniteWith(item->id - 1, item->id + 1, 1);
	}
	void uniteWith(HistoryGroup *other) {
		uniteWith(other->_minId, other->_maxId, other->_count);
	}

	bool decrementCount(); // returns true if result count > 0

	MsgId minId() const {
		return _minId;
	}
	MsgId maxId() const {
		return _maxId;
	}

private:
	MsgId _minId, _maxId;
	int32 _count;

	TextLinkPtr _lnk;

	void updateText();

};

class HistoryCollapse : public HistoryServiceMsg {
public:

	HistoryCollapse(History *history, HistoryBlock *block, MsgId wasMinId, const QDateTime &date);
	void draw(Painter &p, uint32 selection) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
		symbol = 0xFFFF;
		after = false;
		upon = false;
	}
	QString selectedText(uint32 selection) const {
		return QString();
	}
	HistoryItemType type() const {
		return HistoryItemCollapse;
	}
	MsgId wasMinId() const {
		return _wasMinId;
	}

private:
	MsgId _wasMinId;

};

class HistoryJoined : public HistoryServiceMsg {
public:

	HistoryJoined(History *history, HistoryBlock *block, const QDateTime &date, UserData *from, int32 flags);
	HistoryItemType type() const {
		return HistoryItemJoined;
	}
};

HistoryItem *createDayServiceMsg(History *history, HistoryBlock *block, QDateTime date);

class HistoryUnreadBar : public HistoryItem {
public:

	HistoryUnreadBar(History *history, HistoryBlock *block, int32 count, const QDateTime &date);

	void initDimensions();

	void setCount(int32 count);

	void draw(Painter &p, uint32 selection) const;
	int32 resize(int32 width);

	void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
    QString notificationText() const;

	QString selectedText(uint32 selection) const {
		return QString();
	}
	HistoryItemType type() const {
		return HistoryItemUnreadBar;
	}

protected:

	QString text;
	bool freezed;
};

const TextParseOptions &itemTextParseOptions(History *h, PeerData *f);
