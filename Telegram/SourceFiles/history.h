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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

void historyInit();

class HistoryItem;

typedef QMap<int32, HistoryItem*> SelectedItemSet;

#include "structs.h"

enum NewMessageType {
	NewMessageUnread,
	NewMessageLast,
	NewMessageExisting,
};

class History;
class Histories {
public:
	typedef QHash<PeerId, History*> Map;
	Map map;

	Histories() : _a_typings(animation(this, &Histories::step_typings)), unreadFull(0), unreadMuted(0) {
	}

	void regSendAction(History *history, UserData *user, const MTPSendMessageAction &action);
	void step_typings(uint64 ms, bool timer);

	History *find(const PeerId &peerId);
	History *findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead);

	void clear();
	void remove(const PeerId &peer);
	~Histories() {
		unreadFull = unreadMuted = 0;
	}

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	//	HistoryItem *addToBack(const MTPgeoChatMessage &msg, bool newMsg = true);

	typedef QMap<History*, uint64> TypingHistories; // when typing in this history started
	TypingHistories typing;
	Animation _a_typings;

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
	MediaTypeGif,
	MediaTypeSticker,
	MediaTypeImageLink,
	MediaTypeWebPage,

	MediaTypeCount
};

enum MediaOverviewType {
	OverviewPhotos,
	OverviewVideos,
	OverviewAudioDocuments,
	OverviewDocuments,
	OverviewAudios,
	OverviewLinks,

	OverviewCount
};

inline MTPMessagesFilter typeToMediaFilter(MediaOverviewType &type) {
	switch (type) {
	case OverviewPhotos: return MTP_inputMessagesFilterPhotos();
	case OverviewVideos: return MTP_inputMessagesFilterVideo();
	case OverviewAudioDocuments: return MTP_inputMessagesFilterAudioDocuments();
	case OverviewDocuments: return MTP_inputMessagesFilterDocument();
	case OverviewAudios: return MTP_inputMessagesFilterAudio();
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

enum AddToOverviewMethod {
	AddToOverviewNew, // when new message is added to history
	AddToOverviewFront, // when old messages slice was received
	AddToOverviewBack, // when new messages slice was received and it is the last one, we index all media
};

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
	bool isMegagroup() const {
		return peer->isMegagroup();
	}
	ChannelHistory *asChannelHistory();
	const ChannelHistory *asChannelHistory() const;

	bool isEmpty() const {
		return blocks.isEmpty();
	}
	void clear(bool leaveItems = false);
	void clearUpto(MsgId msgId);
	void blockResized(HistoryBlock *block, int32 dh);
	void removeBlock(HistoryBlock *block);

	virtual ~History() {
		clear();
	}

	HistoryItem *createItem(HistoryBlock *block, const MTPMessage &msg, bool applyServiceAction);
	HistoryItem *createItemForwarded(HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg);
	HistoryItem *createItemDocument(HistoryBlock *block, MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption);
	HistoryItem *createItemPhoto(HistoryBlock *block, MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption);

	HistoryItem *addNewService(MsgId msgId, QDateTime date, const QString &text, int32 flags = 0, HistoryMedia *media = 0, bool newMsg = true);
	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addToHistory(const MTPMessage &msg);
	HistoryItem *addNewForwarded(MsgId id, QDateTime date, int32 from, HistoryMessage *item);
	HistoryItem *addNewDocument(MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption);
	HistoryItem *addNewPhoto(MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption);

	void addOlderSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed);
	void addNewerSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed);
	bool addToOverview(MediaOverviewType type, MsgId msgId, AddToOverviewMethod method);
	void eraseFromOverview(MediaOverviewType type, MsgId msgId);

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

	int32 geomResize(int32 newWidth, int32 *ytransform = 0, const HistoryItem *resizedItem = 0); // return new size

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

	void paintDialog(Painter &p, int32 w, bool sel) const;
	bool updateTyping(uint64 ms, bool force = false);
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
	MsgId lastKeyboardId, lastKeyboardHiddenId;
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
	uint32 typingDots;
	QMap<SendActionType, uint64> mySendActions;

	typedef QList<MsgId> MediaOverview;
	MediaOverview overview[OverviewCount];

	bool overviewCountLoaded(int32 overviewIndex) const {
		return overviewCountData[overviewIndex] >= 0;
	}
	bool overviewLoaded(int32 overviewIndex) const {
		return overviewCount(overviewIndex) == overview[overviewIndex].size();
	}
	int32 overviewCount(int32 overviewIndex, int32 defaultValue = -1) const {
		int32 result = overviewCountData[overviewIndex], loaded = overview[overviewIndex].size();
		if (result < 0) return defaultValue;
		if (result < loaded) {
			if (result > 0) {
				const_cast<History*>(this)->overviewCountData[overviewIndex] = 0;
			}
			return loaded;
		}
		return result;
	}
	MsgId overviewMinId(int32 overviewIndex) const {
		for (MediaOverviewIds::const_iterator i = overviewIds[overviewIndex].cbegin(), e = overviewIds[overviewIndex].cend(); i != e; ++i) {
			if (i.key() > 0) {
				return i.key();
			}
		}
		return 0;
	}
	void overviewSliceDone(int32 overviewIndex, const MTPmessages_Messages &result, bool onlyCounts = false);
	bool overviewHasMsgId(int32 overviewIndex, MsgId msgId) const {
		return overviewIds[overviewIndex].constFind(msgId) != overviewIds[overviewIndex].cend();
	}

	void changeMsgId(MsgId oldId, MsgId newId);

private:

	typedef QMap<MsgId, NullType> MediaOverviewIds;
	MediaOverviewIds overviewIds[OverviewCount];
	int32 overviewCountData[OverviewCount]; // -1 - not loaded, 0 - all loaded, > 0 - count, but not all loaded

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
			bool active = (drawFrom->history->peer == act) || (drawFrom->history->peer->migrateTo() && drawFrom->history->peer->migrateTo() == act);
			bool selected = (drawFrom->history->peer == sel);
			drawFrom->paint(p, w, active, selected, onlyBackground);
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

	int32 geomResize(int32 newWidth, int32 *ytransform, const HistoryItem *resizedItem); // return new size
	int32 y, height;
	History *history;
};

class HistoryElem {
public:

	HistoryElem() : _maxw(0), _minh(0), _height(0) {
	}

	int32 maxWidth() const {
		return _maxw;
	}
	int32 minHeight() const {
		return _minh;
	}
	int32 height() const {
		return _height;
	}

	virtual ~HistoryElem() {
	}

protected:

	mutable int32 _maxw, _minh, _height;
	HistoryElem &operator=(const HistoryElem &);

};

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
	return (flags & MTPDmessage::flag_out) || (flags & MTPDmessage::flag_mentioned) || ((id > 0 || flags != 0) && !(flags & MTPDmessage::flag_from_id));
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
	virtual void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const = 0;

	virtual UserData *viaBot() const {
		return 0;
	}

	History *history() const {
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
	virtual void destroy();
	void detach();
	void detachFast();
	bool detached() const {
		return !_block;
	}
	void attach(HistoryBlock *block) {
		_block = block;
	}
	bool out() const {
		return _flags & MTPDmessage::flag_out;
	}
	bool unread() const {
		if (out() && id > 0 && id < _history->outboxReadBefore) return false;
		if (!out() && id > 0) {
			if (id < _history->inboxReadBefore) return false;
			if (channelId() != NoChannel) return true; // no unread flag for incoming messages in channels
		}
		if (history()->peer->isSelf()) return false; // messages from myself are always read
		if (out() && history()->peer->migrateTo()) return false; // outgoing messages in converted chats are always read
		return (_flags & MTPDmessage::flag_unread);
	}
	bool mentionsMe() const {
		return _flags & MTPDmessage::flag_mentioned;
	}
	bool isMediaUnread() const {
		return (_flags & MTPDmessage::flag_media_unread) && (channelId() == NoChannel);
	}
	void markMediaRead() {
		_flags &= ~MTPDmessage::flag_media_unread;
	}
	bool hasReplyMarkup() const {
		return _flags & MTPDmessage::flag_reply_markup;
	}
	bool hasTextLinks() const {
		return _flags & MTPDmessage_flag_HAS_TEXT_LINKS;
	}
	bool isGroupMigrate() const {
		return _flags & MTPDmessage_flag_IS_GROUP_MIGRATE;
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
	bool indexInOverview() const {
		return (id > 0) && (!history()->isChannel() || history()->isMegagroup() || fromChannel());
	}

	virtual bool needCheck() const {
		return out() || (id < 0 && history()->peer->isSelf());
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
	virtual void linkOver(const TextLinkPtr &lnk) {
	}
	virtual void linkOut(const TextLinkPtr &lnk) {
	}
	virtual HistoryItemType type() const {
		return HistoryItemMsg;
	}
	virtual bool serviceMsg() const {
		return false;
	}
	virtual void updateMedia(const MTPMessageMedia *media) {
	}
	virtual int32 addToOverview(AddToOverviewMethod method) {
		return 0;
	}
	virtual bool hasBubble() const {
		return false;
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

	virtual void drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	}
	virtual void setViewsCount(int32 count, bool reinit = true) {
	}
	virtual void setId(MsgId newId);
	virtual void setDate(const QDateTime &date) { // for date items
		this->date = date;
	}
	virtual void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const = 0;
    virtual QString notificationHeader() const {
        return QString();
    }
    virtual QString notificationText() const = 0;

	bool canDelete() const {
		ChannelData *channel = _history->peer->asChannel();
		if (!channel) return !(_flags & MTPDmessage_flag_IS_GROUP_MIGRATE);

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
	virtual void setText(const QString &text, const EntitiesInText &links) {
	}
	virtual QString originalText() const {
		return QString();
	}
	virtual EntitiesInText originalEntities() const {
		return EntitiesInText();
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

	bool hasFromName() const {
		return (!out() || fromChannel()) && !history()->peer->isUser();
	}
	bool displayFromPhoto() const;
	bool shiftFromPhoto() const;

	void clipCallback(ClipReaderNotification notification);

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
	MessageLink(HistoryItem *item) : _peer(item->history()->peer->id), _msgid(item->id) {
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

HistoryItem *regItem(HistoryItem *item);

class RadialAnimation {
public:

	RadialAnimation(AnimationCreator creator);

	float64 opacity() const {
		return _opacity;
	}
	bool animating() const {
		return _animation.animating();
	}

	void start(float64 prg);
	void update(float64 prg, bool finished, uint64 ms);
	void stop();

	void step(uint64 ms);
	void step() {
		step(getms());
	}

	void draw(Painter &p, const QRect &inner, int32 thickness, const style::color &color);

private:

	uint64 _firstStart, _lastStart, _lastTime;
	float64 _opacity;
	anim::ivalue a_arcEnd, a_arcStart;
	Animation _animation;

};

class HistoryMedia : public HistoryElem {
public:

	HistoryMedia() : _width(0) {
	}
	HistoryMedia(const HistoryMedia &other) : _width(0) {
	}

	virtual HistoryMediaType type() const = 0;
	virtual const QString inDialogsText() const = 0;
	virtual const QString inHistoryText() const = 0;

	bool hasPoint(int32 x, int32 y, const HistoryItem *parent) const {
		return (x >= 0 && y >= 0 && x < _width && y < _height);
	}

	virtual bool isDisplayed() const {
		return true;
	}
	virtual void initDimensions(const HistoryItem *parent) = 0;
	virtual int32 resize(int32 width, const HistoryItem *parent) { // return new height
		_width = qMin(width, _maxw);
		return _height;
	}
	virtual void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const = 0;
	virtual void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const = 0;

	virtual void linkOver(HistoryItem *parent, const TextLinkPtr &lnk) {
	}
	virtual void linkOut(HistoryItem *parent, const TextLinkPtr &lnk) {
	}

	virtual bool uploading() const {
		return false;
	}
	virtual HistoryMedia *clone() const = 0;

	virtual DocumentData *getDocument() {
		return 0;
	}
	virtual ClipReader *getClipReader() {
		return 0;
	}

	virtual bool playInline(HistoryItem *item) {
		return false;
	}
	virtual void stopInline(HistoryItem *item) {
	}

	virtual void regItem(HistoryItem *item) {
	}

	virtual void unregItem(HistoryItem *item) {
	}

	virtual void updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
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
	virtual bool needsBubble(const HistoryItem *parent) const = 0;
	virtual bool customInfoLayout() const = 0;
	virtual QMargins bubbleMargins() const {
		return QMargins();
	}
	virtual bool hideFromName() const {
		return false;
	}
	virtual bool hideForwardedFrom() const {
		return false;
	}

	int32 currentWidth() const {
		return _width;
	}

protected:

	int32 _width;

};

inline MediaOverviewType mediaToOverviewType(HistoryMedia *media) {
	switch (media->type()) {
	case MediaTypePhoto: return OverviewPhotos;
	case MediaTypeVideo: return OverviewVideos;
	case MediaTypeDocument: return media->getDocument()->song() ? OverviewAudioDocuments : OverviewDocuments;
	case MediaTypeGif: return media->getDocument()->isGifv() ? OverviewCount : OverviewDocuments;
//	case MediaTypeSticker: return OverviewDocuments;
	case MediaTypeAudio: return OverviewAudios;
	}
	return OverviewCount;
}

class HistoryFileMedia : public HistoryMedia {
public:

	HistoryFileMedia();

	void linkOver(HistoryItem *parent, const TextLinkPtr &lnk);
	void linkOut(HistoryItem *parent, const TextLinkPtr &lnk);

	~HistoryFileMedia();

protected:

	TextLinkPtr _openl, _savel, _cancell;
	void setLinks(ITextLink *openl, ITextLink *savel, ITextLink *cancell);

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int32 _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const;

	void step_thumbOver(const HistoryItem *parent, float64 ms, bool timer);
	void step_radial(const HistoryItem *parent, uint64 ms, bool timer);

	void ensureAnimation(const HistoryItem *parent) const;
	void checkAnimationFinished();

	bool isRadialAnimation(uint64 ms) const {
		if (!_animation || !_animation->radial.animating()) return false;

		_animation->radial.step(ms);
		return _animation && _animation->radial.animating();
	}
	bool isThumbAnimation(uint64 ms) const {
		if (!_animation || !_animation->_a_thumbOver.animating()) return false;

		_animation->_a_thumbOver.step(ms);
		return _animation && _animation->_a_thumbOver.animating();
	}

	virtual float64 dataProgress() const = 0;
	virtual bool dataFinished() const = 0;
	virtual bool dataLoaded() const = 0;

	struct AnimationData {
		AnimationData(AnimationCreator thumbOverCallbacks, AnimationCreator radialCallbacks) : a_thumbOver(0, 0)
			, _a_thumbOver(thumbOverCallbacks)
			, radial(radialCallbacks) {
		}
		anim::fvalue a_thumbOver;
		Animation _a_thumbOver;

		RadialAnimation radial;
	};
	mutable AnimationData *_animation;

private:

	HistoryFileMedia(const HistoryFileMedia &other);

};

class HistoryPhoto : public HistoryFileMedia {
public:

	HistoryPhoto(PhotoData *photo, const QString &caption, const HistoryItem *parent);
	HistoryPhoto(PeerData *chat, const MTPDphoto &photo, int32 width = 0);
	HistoryPhoto(const HistoryPhoto &other);
	void init();
	HistoryMediaType type() const {
		return MediaTypePhoto;
	}
	HistoryMedia *clone() const {
		return new HistoryPhoto(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	PhotoData *photo() const {
		return _data;
	}

	void updateFrom(const MTPMessageMedia &media, HistoryItem *parent);

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	bool hasReplyPreview() const {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview();

	QString getCaption() const {
		return _caption.original();
	}
	bool needsBubble(const HistoryItem *parent) const {
		return !_caption.isEmpty() || parent->toHistoryForwarded() || parent->toHistoryReply() || parent->viaBot();
	}
	bool customInfoLayout() const {
		return _caption.isEmpty();
	}
	bool hideFromName() const {
		return true;
	}

protected:

	float64 dataProgress() const {
		return _data->progress();
	}
	bool dataFinished() const {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const {
		return _data->loaded();
	}

private:
	PhotoData *_data;
	int16 _pixw, _pixh;
	Text _caption;

};

class HistoryVideo : public HistoryFileMedia {
public:

	HistoryVideo(const MTPDvideo &video, const QString &caption, HistoryItem *parent);
	HistoryVideo(const HistoryVideo &other);
	HistoryMediaType type() const {
		return MediaTypeVideo;
	}
	HistoryMedia *clone() const {
		return new HistoryVideo(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	VideoData *video() const {
		return _data;
	}

	bool uploading() const {
		return _data->uploading();
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	bool hasReplyPreview() const {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview();

	bool needsBubble(const HistoryItem *parent) const {
		return !_caption.isEmpty() || parent->toHistoryForwarded() || parent->toHistoryReply() || parent->viaBot();
	}
	bool customInfoLayout() const {
		return _caption.isEmpty();
	}
	bool hideFromName() const {
		return true;
	}

protected:

	float64 dataProgress() const {
		return _data->progress();
	}
	bool dataFinished() const {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const {
		return _data->loaded();
	}

private:
	VideoData *_data;
	int16 _thumbw;
	Text _caption;

	void setStatusSize(int32 newSize) const;
	void updateStatusText(const HistoryItem *parent) const;

};

class HistoryAudio : public HistoryFileMedia {
public:

	HistoryAudio(const MTPDaudio &audio);
	HistoryAudio(const HistoryAudio &other);
	HistoryMediaType type() const {
		return MediaTypeAudio;
	}
	HistoryMedia *clone() const {
		return new HistoryAudio(*this);
	}

	void initDimensions(const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	bool uploading() const {
		return _data->uploading();
	}

	AudioData *audio() {
		return _data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media, HistoryItem *parent);

	bool needsBubble(const HistoryItem *parent) const {
		return true;
	}
	bool customInfoLayout() const {
		return false;
	}
	QMargins bubbleMargins() const {
		return st::msgPadding;
	}

protected:

	float64 dataProgress() const {
		return _data->progress();
	}
	bool dataFinished() const {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const {
		return _data->loaded();
	}

private:
	AudioData *_data;

	void setStatusSize(int32 newSize, qint64 realDuration = 0) const;
	bool updateStatusText(const HistoryItem *parent) const; // returns showPause

};

class HistoryDocument : public HistoryFileMedia {
public:

	HistoryDocument(DocumentData *document, const QString &caption, const HistoryItem *parent);
	HistoryDocument(const HistoryDocument &other);
	HistoryMediaType type() const {
		return MediaTypeDocument;
	}
	HistoryMedia *clone() const {
		return new HistoryDocument(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	bool uploading() const {
		return _data->uploading();
	}

	bool withThumb() const {
		return !_data->song() && !_data->thumb->isNull() && _data->thumb->width() && _data->thumb->height();
	}

	DocumentData *getDocument() {
		return _data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media, HistoryItem *parent);

	bool hasReplyPreview() const {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview();

	QString getCaption() const {
		return _caption.original();
	}
	bool needsBubble(const HistoryItem *parent) const {
		return true;
	}
	bool customInfoLayout() const {
		return false;
	}
	QMargins bubbleMargins() const {
		return withThumb() ? QMargins(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbPadding.left(), st::msgFileThumbPadding.bottom()) : st::msgPadding;
	}
	bool hideForwardedFrom() const {
		return _data->song();
	}

protected:

	float64 dataProgress() const {
		return _data->progress();
	}
	bool dataFinished() const {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const {
		return _data->loaded();
	}

private:

	DocumentData *_data;
	TextLinkPtr _linksavel, _linkcancell;

	QString _name;
	int32 _namew;
	int32 _thumbw;

	mutable int32 _linkw;
	mutable QString _link;

	Text _caption;

	void setStatusSize(int32 newSize, qint64 realDuration = 0) const;
	bool updateStatusText(const HistoryItem *parent) const; // returns showPause

};

class HistoryGif : public HistoryFileMedia {
public:

	HistoryGif(DocumentData *document, const QString &caption, const HistoryItem *parent);
	HistoryGif(const HistoryGif &other);
	HistoryMediaType type() const {
		return MediaTypeGif;
	}
	HistoryMedia *clone() const {
		return new HistoryGif(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	bool uploading() const {
		return _data->uploading();
	}

	DocumentData *getDocument() {
		return _data;
	}
	ClipReader *getClipReader() {
		return gif();
	}

	bool playInline(HistoryItem *item);
	void stopInline(HistoryItem *item);

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media, HistoryItem *parent);

	bool hasReplyPreview() const {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview();

	QString getCaption() const {
		return _caption.original();
	}
	bool needsBubble(const HistoryItem *parent) const {
		return !_caption.isEmpty() || parent->toHistoryForwarded() || parent->toHistoryReply() || parent->viaBot();
	}
	bool customInfoLayout() const {
		return _caption.isEmpty();
	}
	bool hideFromName() const {
		return true;
	}

	~HistoryGif();

protected:

	float64 dataProgress() const;
	bool dataFinished() const;
	bool dataLoaded() const;

private:

	const HistoryItem *_parent;
	DocumentData *_data;
	int32 _thumbw, _thumbh;
	Text _caption;

	ClipReader *_gif;
	ClipReader *gif() {
		return (_gif == BadClipReader) ? 0 : _gif;
	}
	const ClipReader *gif() const {
		return (_gif == BadClipReader) ? 0 : _gif;
	}

	void setStatusSize(int32 newSize) const;
	void updateStatusText(const HistoryItem *parent) const;

};

class HistorySticker : public HistoryMedia {
public:

	HistorySticker(DocumentData *document);
	HistoryMediaType type() const {
		return MediaTypeSticker;
	}
	HistoryMedia *clone() const {
		return new HistorySticker(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	DocumentData *getDocument() {
		return _data;
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media, HistoryItem *parent);

	bool needsBubble(const HistoryItem *parent) const {
		return false;
	}
	bool customInfoLayout() const {
		return true;
	}

private:

	int16 _pixw, _pixh;
	DocumentData *_data;
	QString _emoji;

};

class SendMessageLink : public PeerLink {
	TEXT_LINK_CLASS(SendMessageLink)

public:
	SendMessageLink(PeerData *peer) : PeerLink(peer) {
	}
	void onClick(Qt::MouseButton button) const;

};

class AddContactLink : public MessageLink {
	TEXT_LINK_CLASS(AddContactLink)

public:
	AddContactLink(PeerId peer, MsgId msgid) : MessageLink(peer, msgid) {
	}
	void onClick(Qt::MouseButton button) const;

};

class HistoryContact : public HistoryMedia {
public:

	HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone);
	HistoryMediaType type() const {
		return MediaTypeContact;
	}
	HistoryMedia *clone() const {
		return new HistoryContact(_userId, _fname, _lname, _phone);
	}

	void initDimensions(const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	void updateFrom(const MTPMessageMedia &media, HistoryItem *parent);

	bool needsBubble(const HistoryItem *parent) const {
		return true;
	}
	bool customInfoLayout() const {
		return false;
	}

	const QString &fname() const {
		return _fname;
	}
	const QString &lname() const {
		return _lname;
	}
	const QString &phone() const {
		return _phone;
	}

private:

	int32 _userId;
	UserData *_contact;

	int32 _phonew;
	QString _fname, _lname, _phone;
	Text _name;

	TextLinkPtr _linkl;
	int32 _linkw;
	QString _link;
};

class HistoryWebPage : public HistoryMedia {
public:

	HistoryWebPage(WebPageData *data);
	HistoryWebPage(const HistoryWebPage &other);
	HistoryMediaType type() const {
		return MediaTypeWebPage;
	}
	HistoryMedia *clone() const {
		return new HistoryWebPage(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	void linkOver(HistoryItem *parent, const TextLinkPtr &lnk);
	void linkOut(HistoryItem *parent, const TextLinkPtr &lnk);

	bool isDisplayed() const {
		return !_data->pendingTill;
	}
	DocumentData *getDocument() {
		return _attach ? _attach->getDocument() : 0;
	}
	ClipReader *getClipReader() {
		return _attach ? _attach->getClipReader() : 0;
	}
	bool playInline(HistoryItem *item) {
		return _attach ? _attach->playInline(item) : false;
	}
	void stopInline(HistoryItem *item) {
		if (_attach) _attach->stopInline(item);
	}

	void regItem(HistoryItem *item);
	void unregItem(HistoryItem *item);

	bool hasReplyPreview() const {
		return (_data->photo && !_data->photo->thumb->isNull()) || (_data->doc && !_data->doc->thumb->isNull());
	}
	ImagePtr replyPreview();

	WebPageData *webpage() {
		return _data;
	}

	bool needsBubble(const HistoryItem *parent) const {
		return true;
	}
	bool customInfoLayout() const {
		return false;
	}

	HistoryMedia *attach() const {
		return _attach;
	}

	~HistoryWebPage();

private:
	WebPageData *_data;
	TextLinkPtr _openl;
	HistoryMedia *_attach;

	bool _asArticle;
	int32 _titleLines, _descriptionLines;

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
	GoogleMapsLink
};
struct ImageLinkData {
	ImageLinkData(const QString &id) : id(id), type(InvalidImageLink), loading(false) {
	}

	QString id;
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
	HistoryMediaType type() const {
		return MediaTypeImageLink;
	}
	HistoryMedia *clone() const {
		return new HistoryImageLink(*this);
	}

	void initDimensions(const HistoryItem *parent);
	int32 resize(int32 width, const HistoryItem *parent);

	void draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const;

	const QString inDialogsText() const;
	const QString inHistoryText() const;

	bool isImageLink() const {
		return true;
	}

	bool needsBubble(const HistoryItem *parent) const {
		return !_title.isEmpty() || !_description.isEmpty() || parent->toHistoryForwarded() || parent->toHistoryReply() || parent->viaBot();
	}
	bool customInfoLayout() const {
		return true;
	}

private:
	ImageLinkData *_data;
	Text _title, _description;
	TextLinkPtr _link;

	int32 fullWidth() const;
	int32 fullHeight() const;

};

class ViaInlineBotLink : public ITextLink {
	TEXT_LINK_CLASS(ViaInlineBotLink)

public:
	ViaInlineBotLink(UserData *bot) : _bot(bot) {
	}
	void onClick(Qt::MouseButton button) const;

private:
	UserData *_bot;

};

class HistoryMessageVia {
public:
	HistoryMessageVia(int32 userId);

	bool isNull() const;
	void resize(int32 availw);

	UserData *bot;
	QString text;
	int32 width, maxWidth;
	TextLinkPtr lnk;

};

class HistoryMessage : public HistoryItem {
public:

	HistoryMessage(History *history, HistoryBlock *block, const MTPDmessage &msg);
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, QDateTime date, int32 from, const QString &msg, const EntitiesInText &entities, HistoryMedia *media); // local forwarded
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, QDateTime date, int32 from, DocumentData *doc, const QString &caption); // local document
	HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, QDateTime date, int32 from, PhotoData *photo, const QString &caption); // local photo

	void initTime();
	void initMedia(const MTPMessageMedia *media, QString &currentText);
	void initMediaFromDocument(DocumentData *doc, const QString &caption);
	void initDimensions();
	void fromNameUpdated(int32 width) const;

	virtual HistoryMessageVia *via() const {
		return (_via && !_via->isNull()) ? _via : 0;
	}
	virtual UserData *viaBot() const {
		return via() ? via()->bot : 0;
	}

	int32 plainMaxWidth() const;
	void countPositionAndSize(int32 &left, int32 &width) const;

	bool emptyText() const {
		return _text.isEmpty();
	}
	bool drawBubble() const {
		return _media ? (!emptyText() || _media->needsBubble(this)) : true;
	}
	bool hasBubble() const {
		return drawBubble();
	}
	bool displayFromName() const {
		return hasFromName() && (!emptyText() || !_media || !_media->isDisplayed() || toHistoryReply() || toHistoryForwarded() || viaBot() || !_media->hideFromName());
	}
	bool uploading() const {
		return _media && _media->uploading();
	}

	void drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const;
	void setViewsCount(int32 count, bool reinit = true);
	void setId(MsgId newId);
	void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const;

	virtual void drawMessageText(Painter &p, QRect trect, uint32 selection) const;

	void destroy();

	int32 resize(int32 width);
	bool hasPoint(int32 x, int32 y) const;
	bool pointInTime(int32 right, int32 bottom, int32 x, int32 y, InfoDisplayType type) const;

	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	virtual void getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const;

	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return _text.adjustSelection(from, to, type);
	}
	void linkOver(const TextLinkPtr &lnk) {
		if (_media) _media->linkOver(this, lnk);
	}
	void linkOut(const TextLinkPtr &lnk) {
		if (_media) _media->linkOut(this, lnk);
	}

	void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const;
    QString notificationHeader() const;
    QString notificationText() const;

	void updateMedia(const MTPMessageMedia *media) {
		if (media && _media && _media->type() != MediaTypeWebPage) {
			_media->updateFrom(*media, this);
		} else {
			setMedia(media);
		}
	}
	int32 addToOverview(AddToOverviewMethod method);
	void eraseFromOverview();

	QString selectedText(uint32 selection) const;
	QString inDialogsText() const;
	HistoryMedia *getMedia(bool inOverview = false) const;
	void setMedia(const MTPMessageMedia *media);
	void setText(const QString &text, const EntitiesInText &entities);
	QString originalText() const;
	EntitiesInText originalEntities() const;
	bool textHasLinks();

	int32 infoWidth() const {
		int32 result = _timeWidth;
		if (!_viewsText.isEmpty()) {
			result += st::msgDateViewsSpace + _viewsWidth + st::msgDateCheckSpace + st::msgViewsImg.pxWidth();
		} else if (id < 0 && history()->peer->isSelf()) {
			result += st::msgDateCheckSpace + st::msgCheckImg.pxWidth();
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
		} else if (id < 0 && history()->peer->isSelf()) {
			result += st::msgDateCheckSpace + st::msgCheckImg.pxWidth();
		}
		return result;
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
	HistoryMessageVia *_via;

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

	void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const;
	void drawForwardedFrom(Painter &p, int32 x, int32 y, int32 w, bool selected) const;
	void drawMessageText(Painter &p, QRect trect, uint32 selection) const;
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
	bool displayForwardedFrom() const {
		return via() || !_media || !_media->isDisplayed() || (fwdFrom->isChannel() || !_media->hideForwardedFrom());
	}

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
	HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption);
	HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption);

	void initDimensions();

	bool updateReplyTo(bool force = false);
	void replyToNameUpdated() const;
	int32 replyToWidth() const;

	TextLinkPtr replyToLink() const;

	MsgId replyToId() const;
	HistoryItem *replyToMessage() const;
	void replyToReplaced(HistoryItem *oldItem, HistoryItem *newItem);

	void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const;
	void drawReplyTo(Painter &p, int32 x, int32 y, int32 w, bool selected, bool likeService = false) const;
	void drawMessageText(Painter &p, QRect trect, uint32 selection) const;
	int32 resize(int32 width);
	void resizeVia(int32 w) const;
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
	HistoryMessageVia *_replyToVia;
	HistoryMessageVia *replyToVia() const {
		return (_replyToVia && !_replyToVia->isNull()) ? _replyToVia : 0;
	}
	int32 toWidth;

};

inline int32 newMessageFlags(PeerData *p) {
	return p->isSelf() ? 0 : (((p->isChat() || (p->isUser() && !p->asUser()->botInfo)) ? MTPDmessage::flag_unread : 0) | MTPDmessage::flag_out);
}
inline int32 newForwardedFlags(PeerData *p, int32 from, HistoryMessage *msg) {
	return newMessageFlags(p) | (from ? MTPDmessage::flag_from_id : 0) | (msg->via() ? MTPDmessage::flag_via_bot_id : 0) | (!p->isChannel() && msg->getMedia() && (msg->getMedia()->type() == MediaTypeAudio/* || msg->getMedia()->type() == MediaTypeVideo*/) ? MTPDmessage::flag_media_unread : 0);
}

class HistoryServiceMsg : public HistoryItem {
public:

	HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDmessageService &msg);
	HistoryServiceMsg(History *history, HistoryBlock *block, MsgId msgId, QDateTime date, const QString &msg, int32 flags = 0, HistoryMedia *media = 0, int32 from = 0);

	void initDimensions();

	void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const;
	int32 resize(int32 width);
	bool hasPoint(int32 x, int32 y) const;
	void getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const;
	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const;
	uint32 adjustSelection(uint16 from, uint16 to, TextSelectType type) const {
		return _text.adjustSelection(from, to, type);
	}

	void linkOver(const TextLinkPtr &lnk) {
		if (_media) _media->linkOver(this, lnk);
	}
	void linkOut(const TextLinkPtr &lnk) {
		if (_media) _media->linkOut(this, lnk);
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
	void setDate(const QDateTime &date);
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
	void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const;
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

	void draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const;
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
