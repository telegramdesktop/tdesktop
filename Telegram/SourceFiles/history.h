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
#include "dialogs/dialogs_common.h"

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

	Histories() : _a_typings(animation(this, &Histories::step_typings)), _unreadFull(0), _unreadMuted(0) {
	}

	void regSendAction(History *history, UserData *user, const MTPSendMessageAction &action);
	void step_typings(uint64 ms, bool timer);

	History *find(const PeerId &peerId);
	History *findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead);

	void clear();
	void remove(const PeerId &peer);
	~Histories() {
		_unreadFull = _unreadMuted = 0;
	}

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);

	typedef QMap<History*, uint64> TypingHistories; // when typing in this history started
	TypingHistories typing;
	Animation _a_typings;

	int32 unreadBadge() const {
		return _unreadFull - (cIncludeMuted() ? 0 : _unreadMuted);
	}
	int32 unreadMutedCount() const {
		return _unreadMuted;
	}
	bool unreadOnlyMuted() const {
		return cIncludeMuted() ? (_unreadMuted >= _unreadFull) : false;
	}
	void unreadIncrement(int32 count, bool muted) {
		_unreadFull += count;
		if (muted) {
			_unreadMuted += count;
		}
	}
	void unreadMuteChanged(int32 count, bool muted) {
		if (muted) {
			_unreadMuted += count;
		} else {
			_unreadMuted -= count;
		}
	}

private:
	int32 _unreadFull, _unreadMuted;

};

class HistoryBlock;

enum HistoryMediaType {
	MediaTypePhoto,
	MediaTypeVideo,
	MediaTypeContact,
	MediaTypeFile,
	MediaTypeGif,
	MediaTypeSticker,
	MediaTypeLocation,
	MediaTypeWebPage,
	MediaTypeMusicFile,
	MediaTypeVoiceFile,

	MediaTypeCount
};

enum MediaOverviewType {
	OverviewPhotos     = 0,
	OverviewVideos     = 1,
	OverviewMusicFiles = 2,
	OverviewFiles      = 3,
	OverviewVoiceFiles = 4,
	OverviewLinks      = 5,

	OverviewCount
};

inline MTPMessagesFilter typeToMediaFilter(MediaOverviewType &type) {
	switch (type) {
	case OverviewPhotos: return MTP_inputMessagesFilterPhotos();
	case OverviewVideos: return MTP_inputMessagesFilterVideo();
	case OverviewMusicFiles: return MTP_inputMessagesFilterMusic();
	case OverviewFiles: return MTP_inputMessagesFilterDocument();
	case OverviewVoiceFiles: return MTP_inputMessagesFilterVoice();
	case OverviewLinks: return MTP_inputMessagesFilterUrl();
	case OverviewCount: break;
	default: type = OverviewCount; break;
	}
	return MTPMessagesFilter();
}

enum SendActionType {
	SendActionTyping,
	SendActionRecordVideo,
	SendActionUploadVideo,
	SendActionRecordVoice,
	SendActionUploadVoice,
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

struct HistoryDraft {
	HistoryDraft() : msgId(0), previewCancelled(false) {
	}
	HistoryDraft(const QString &text, MsgId msgId, const MessageCursor &cursor, bool previewCancelled)
		: text(text)
		, msgId(msgId)
		, cursor(cursor)
		, previewCancelled(previewCancelled) {
	}
	HistoryDraft(const FlatTextarea &field, MsgId msgId, bool previewCancelled)
		: text(field.getLastText())
		, msgId(msgId)
		, cursor(field)
		, previewCancelled(previewCancelled) {
	}
	QString text;
	MsgId msgId; // replyToId for message draft, editMsgId for edit draft
	MessageCursor cursor;
	bool previewCancelled;
};
struct HistoryEditDraft : public HistoryDraft {
	HistoryEditDraft()
		: HistoryDraft()
		, saveRequest(0) {
	}
	HistoryEditDraft(const QString &text, MsgId msgId, const MessageCursor &cursor, bool previewCancelled, mtpRequestId saveRequest = 0)
		: HistoryDraft(text, msgId, cursor, previewCancelled)
		, saveRequest(saveRequest) {
	}
	HistoryEditDraft(const FlatTextarea &field, MsgId msgId, bool previewCancelled, mtpRequestId saveRequest = 0)
		: HistoryDraft(field, msgId, previewCancelled)
		, saveRequest(saveRequest) {
	}
	mtpRequestId saveRequest;
};

class HistoryMedia;
class HistoryMessage;

enum AddToOverviewMethod {
	AddToOverviewNew, // when new message is added to history
	AddToOverviewFront, // when old messages slice was received
	AddToOverviewBack, // when new messages slice was received and it is the last one, we index all media
};

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

class ChannelHistory;
class History {
public:

	History(const PeerId &peerId);
	History(const History &) = delete;
	History &operator=(const History &) = delete;

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

	virtual ~History();

	HistoryItem *addNewService(MsgId msgId, QDateTime date, const QString &text, MTPDmessage::Flags flags = 0, bool newMsg = true);
	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addToHistory(const MTPMessage &msg);
	HistoryItem *addNewForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *item);
	HistoryItem *addNewDocument(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup);
	HistoryItem *addNewPhoto(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup);

	void addOlderSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed);
	void addNewerSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed);
	bool addToOverview(MediaOverviewType type, MsgId msgId, AddToOverviewMethod method);
	void eraseFromOverview(MediaOverviewType type, MsgId msgId);

	void newItemAdded(HistoryItem *item);
	void unregTyping(UserData *from);

	int countUnread(MsgId upTo);
	void updateShowFrom();
	MsgId inboxRead(MsgId upTo);
	MsgId inboxRead(HistoryItem *wasRead);
	MsgId outboxRead(MsgId upTo);
	MsgId outboxRead(HistoryItem *wasRead);

	HistoryItem *lastImportantMessage() const;

	int unreadCount() const {
		return _unreadCount;
	}
	void setUnreadCount(int newUnreadCount);
	bool mute() const {
		return _mute;
	}
	void setMute(bool newMute);
	void getNextShowFrom(HistoryBlock *block, int i);
	void addUnreadBar();
	void destroyUnreadBar();
	void clearNotifications();

	bool loadedAtBottom() const; // last message is in the list
	void setNotLoadedAtBottom();
	bool loadedAtTop() const; // nothing was added after loading history back
	bool isReadyFor(MsgId msgId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop); // has messages for showing history at msgId
	void getReadyFor(MsgId msgId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop);

	void setLastMessage(HistoryItem *msg);
	void fixLastMessage(bool wasAtBottom);

	void setChatsListDate(const QDateTime &date);
	uint64 sortKeyInChatList() const {
		return _sortKeyInChatList;
	}
	struct PositionInChatListChange {
		int movedFrom;
		int movedTo;
	};
	PositionInChatListChange adjustByPosInChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed);
	bool inChatList(Dialogs::Mode list) const {
		return !chatListLinks(list).isEmpty();
	}
	int posInChatList(Dialogs::Mode list) const;
	Dialogs::Row *addToChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed);
	void removeFromChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed);
	void removeChatListEntryByLetter(Dialogs::Mode list, QChar letter);
	void addChatListEntryByLetter(Dialogs::Mode list, QChar letter, Dialogs::Row *row);
	void updateChatListEntry() const;

	MsgId minMsgId() const;
	MsgId maxMsgId() const;
	MsgId msgIdForRead() const;

	int resizeGetHeight(int newWidth);

	void removeNotification(HistoryItem *item) {
		if (!notifies.isEmpty()) {
			for (auto i = notifies.begin(), e = notifies.end(); i != e; ++i) {
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

	bool hasPendingResizedItems() const {
		return _flags & Flag::f_has_pending_resized_items;
	}
	void setHasPendingResizedItems();
	void setPendingResize() {
		_flags |= Flag::f_pending_resize;
		setHasPendingResizedItems();
	}

	void paintDialog(Painter &p, int32 w, bool sel) const;
	bool updateTyping(uint64 ms, bool force = false);
	void clearLastKeyboard();

	// optimization for userpics displayed on the left
	// if this returns false there is no need to even try to handle them
	bool canHaveFromPhotos() const;

	typedef QList<HistoryBlock*> Blocks;
	Blocks blocks;

	int width = 0;
	int height = 0;
	int32 msgCount = 0;
	MsgId inboxReadBefore = 1;
	MsgId outboxReadBefore = 1;
	HistoryItem *showFrom = nullptr;
	HistoryItem *unreadBar = nullptr;

	PeerData *peer;
	bool oldLoaded = false;
	bool newLoaded = true;
	HistoryItem *lastMsg = nullptr;
	HistoryItem *lastSentMsg = nullptr;
	QDateTime lastMsgDate;

	typedef QList<HistoryItem*> NotifyQueue;
	NotifyQueue notifies;

	HistoryDraft *msgDraft() {
		return _msgDraft.get();
	}
	HistoryEditDraft *editDraft() {
		return _editDraft.get();
	}
	void setMsgDraft(std_::unique_ptr<HistoryDraft> &&draft) {
		_msgDraft = std_::move(draft);
	}
	void takeMsgDraft(History *from) {
		if (auto &draft = from->_msgDraft) {
			if (!draft->text.isEmpty() && !_msgDraft) {
				_msgDraft = std_::move(draft);
				_msgDraft->msgId = 0; // edit and reply to drafts can't migrate
			}
			from->clearMsgDraft();
		}
	}
	void setEditDraft(std_::unique_ptr<HistoryEditDraft> &&draft) {
		_editDraft = std_::move(draft);
	}
	void clearMsgDraft() {
		_msgDraft = nullptr;
	}
	void clearEditDraft() {
		_editDraft = nullptr;
	}
	HistoryDraft *draft() {
		return _editDraft ? editDraft() : msgDraft();
	}

	// some fields below are a property of a currently displayed instance of this
	// conversation history not a property of the conversation history itself
public:
	// we save the last showAtMsgId to restore the state when switching
	// between different conversation histories
	MsgId showAtMsgId = ShowAtUnreadMsgId;

	// we save a pointer of the history item at the top of the displayed window
	// together with an offset from the window top to the top of this message
	// resulting scrollTop = top(scrollTopItem) + scrollTopOffset
	HistoryItem *scrollTopItem = nullptr;
	int scrollTopOffset = 0;
	void forgetScrollState() {
		scrollTopItem = nullptr;
	}

	// find the correct scrollTopItem and scrollTopOffset using given top
	// of the displayed window relative to the history start coord
	void countScrollState(int top);

protected:
	// when this item is destroyed scrollTopItem just points to the next one
	// and scrollTopOffset remains the same
	// if we are at the bottom of the window scrollTopItem == nullptr and
	// scrollTopOffset is undefined
	void getNextScrollTopItem(HistoryBlock *block, int32 i);

	// helper method for countScrollState(int top)
	void countScrollTopItem(int top);

public:

	bool lastKeyboardInited = false;
	bool lastKeyboardUsed = false;
	MsgId lastKeyboardId = 0;
	MsgId lastKeyboardHiddenId = 0;
	PeerId lastKeyboardFrom = 0;

	mtpRequestId sendRequestId = 0;

	mutable const HistoryItem *textCachedFor = nullptr; // cache
	mutable Text lastItemTextCache = Text{ int(st::dlgRichMinWidth) };

	typedef QMap<UserData*, uint64> TypingUsers;
	TypingUsers typing;
	typedef QMap<UserData*, SendAction> SendActionUsers;
	SendActionUsers sendActions;
	QString typingStr;
	Text typingText = Text{ int(st::dlgRichMinWidth) };
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

protected:

	void clearOnDestroy();
	HistoryItem *addNewToLastBlock(const MTPMessage &msg, NewMessageType type);

	friend class HistoryBlock;

	// this method just removes a block from the blocks list
	// when the last item from this block was detached and
	// calls the required previousItemChanged()
	void removeBlock(HistoryBlock *block);

	void clearBlocks(bool leaveItems);

	HistoryItem *createItem(const MTPMessage &msg, bool applyServiceAction, bool detachExistingItem);
	HistoryItem *createItemForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *msg);
	HistoryItem *createItemDocument(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup);
	HistoryItem *createItemPhoto(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup);

	HistoryItem *addNewItem(HistoryItem *adding, bool newMsg);
	HistoryItem *addNewInTheMiddle(HistoryItem *newItem, int32 blockIndex, int32 itemIndex);

	// All this methods add a new item to the first or last block
	// depending on if we are in isBuildingFronBlock() state.
	// The last block is created on the go if it is needed.

	// If the previous item is a message group the new group is
	// not created but is just united with the previous one.
	// create(HistoryItem *previous) should return a new HistoryGroup*
	// unite(HistoryGroup *existing) should unite a new group with an existing
	template <typename CreateGroup, typename UniteGroup>
	void addMessageGroup(CreateGroup create, UniteGroup unite);
	void addMessageGroup(const MTPDmessageGroup &group);

	// Adds the item to the back or front block, depending on
	// isBuildingFrontBlock(), creating the block if necessary.
	void addItemToBlock(HistoryItem *item);

	// Usually all new items are added to the last block.
	// Only when we scroll up and add a new slice to the
	// front we want to create a new front block.
	void startBuildingFrontBlock(int expectedItemsCount = 1);
	HistoryBlock *finishBuildingFrontBlock(); // Returns the built block or nullptr if nothing was added.
	bool isBuildingFrontBlock() const {
		return _buildingFrontBlock != nullptr;
	}

private:

	// After adding a new history slice check the lastMsg and newLoaded.
	void checkLastMsg();

	enum class Flag {
		f_has_pending_resized_items = (1 << 0),
		f_pending_resize            = (1 << 1),
	};
	Q_DECLARE_FLAGS(Flags, Flag);
	Q_DECL_CONSTEXPR friend inline QFlags<Flags::enum_type> operator|(Flags::enum_type f1, Flags::enum_type f2) noexcept {
		return QFlags<Flags::enum_type>(f1) | f2;
	}
	Q_DECL_CONSTEXPR friend inline QFlags<Flags::enum_type> operator|(Flags::enum_type f1, QFlags<Flags::enum_type> f2) noexcept {
		return f2 | f1;
	}
	Q_DECL_CONSTEXPR friend inline QFlags<Flags::enum_type> operator~(Flags::enum_type f) noexcept {
		return ~QFlags<Flags::enum_type>(f);
	}
	Flags _flags;
	bool _mute;
	int32 _unreadCount = 0;

	Dialogs::RowsByLetter _chatListLinks[2];
	Dialogs::RowsByLetter &chatListLinks(Dialogs::Mode list) {
		return _chatListLinks[static_cast<int>(list)];
	}
	const Dialogs::RowsByLetter &chatListLinks(Dialogs::Mode list) const {
		return _chatListLinks[static_cast<int>(list)];
	}
	Dialogs::Row *mainChatListLink(Dialogs::Mode list) const {
		auto it = chatListLinks(list).constFind(0);
		t_assert(it != chatListLinks(list).cend());
		return it.value();
	}
	uint64 _sortKeyInChatList = 0; // like ((unixtime) << 32) | (incremented counter)

	typedef QMap<MsgId, NullType> MediaOverviewIds;
	MediaOverviewIds overviewIds[OverviewCount];
	int32 overviewCountData[OverviewCount]; // -1 - not loaded, 0 - all loaded, > 0 - count, but not all loaded

	// A pointer to the block that is currently being built.
	// We hold this pointer so we can destroy it while building
	// and then create a new one if it is necessary.
	struct BuildingBlock {
		int expectedItemsCount = 0; // optimization for block->items.reserve() call
		HistoryBlock *block = nullptr;
	};
	std_::unique_ptr<BuildingBlock> _buildingFrontBlock;

	// Creates if necessary a new block for adding item.
	// Depending on isBuildingFrontBlock() gets front or back block.
	HistoryBlock *prepareBlockForAddingItem();

	std_::unique_ptr<HistoryDraft> _msgDraft;
	std_::unique_ptr<HistoryEditDraft> _editDraft;

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

	~ChannelHistory();

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

	void cleared(bool leaveItems);

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

class HistoryBlock {
public:
	HistoryBlock(History *hist) : y(0), height(0), history(hist), _indexInHistory(-1) {
	}

	HistoryBlock(const HistoryBlock &) = delete;
	HistoryBlock &operator=(const HistoryBlock &) = delete;

	typedef QVector<HistoryItem*> Items;
	Items items;

	void clear(bool leaveItems = false);
	~HistoryBlock() {
		clear();
	}
	void removeItem(HistoryItem *item);

	int resizeGetHeight(int newWidth, bool resizeAllItems);
	int32 y, height;
	History *history;

	HistoryBlock *previous() const {
		t_assert(_indexInHistory >= 0);

		return (_indexInHistory > 0) ? history->blocks.at(_indexInHistory - 1) : nullptr;
	}
	void setIndexInHistory(int index) {
		_indexInHistory = index;
	}
	int indexInHistory() const {
		t_assert(_indexInHistory >= 0);
		t_assert(history->blocks.at(_indexInHistory) == this);

		return _indexInHistory;
	}

protected:

	int _indexInHistory;

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

class HistoryMessage;

enum HistoryCursorState {
	HistoryDefaultCursorState,
	HistoryInTextCursorState,
	HistoryInDateCursorState,
	HistoryInForwardedCursorState,
};

struct HistoryTextState {
	HistoryTextState() = default;
	HistoryTextState(const Text::StateResult &state)
		: cursor(state.uponSymbol ? HistoryInTextCursorState : HistoryDefaultCursorState)
		, link(state.link)
		, afterSymbol(state.afterSymbol)
		, symbol(state.symbol) {
	}
	HistoryTextState &operator=(const Text::StateResult &state) {
		cursor = state.uponSymbol ? HistoryInTextCursorState : HistoryDefaultCursorState;
		link = state.link;
		afterSymbol = state.afterSymbol;
		symbol = state.symbol;
		return *this;
	}
	HistoryCursorState cursor = HistoryDefaultCursorState;
	ClickHandlerPtr link;
	bool afterSymbol = false;
	uint16 symbol = 0;
};

struct HistoryStateRequest {
	Text::StateRequest::Flags flags = Text::StateRequest::Flag::LookupLink;
	Text::StateRequest forText() const {
		Text::StateRequest result;
		result.flags = flags;
		return result;
	}
};

enum InfoDisplayType {
	InfoDisplayDefault,
	InfoDisplayOverImage,
	InfoDisplayOverBackground,
};

inline bool isImportantChannelMessage(MsgId id, MTPDmessage::Flags flags) { // client-side important msgs always has_views or has_from_id
	return (flags & MTPDmessage::Flag::f_out) || (flags & MTPDmessage::Flag::f_mentioned) || (flags & MTPDmessage::Flag::f_post);
}

enum HistoryItemType {
	HistoryItemMsg = 0,
	HistoryItemGroup,
	HistoryItemCollapse,
	HistoryItemJoined
};

struct HistoryMessageVia : public BaseComponent<HistoryMessageVia> {
	void create(int32 userId);
	void resize(int32 availw) const;

	UserData *_bot = nullptr;
	mutable QString _text;
	mutable int _width = 0;
	mutable int _maxWidth = 0;
	ClickHandlerPtr _lnk;
};

struct HistoryMessageViews : public BaseComponent<HistoryMessageViews> {
	QString _viewsText;
	int _views = 0;
	int _viewsWidth = 0;
};

struct HistoryMessageSigned : public BaseComponent<HistoryMessageSigned> {
	void create(UserData *from, const QDateTime &date);
	int maxWidth() const;

	Text _signature;
};

struct HistoryMessageEdited : public BaseComponent<HistoryMessageEdited> {
	void create(const QDateTime &editDate, const QDateTime &date);
	int maxWidth() const;

	QDateTime _editDate;
	Text _edited;
};

struct HistoryMessageForwarded : public BaseComponent<HistoryMessageForwarded> {
	void create(const HistoryMessageVia *via) const;

	PeerData *_authorOriginal = nullptr;
	PeerData *_fromOriginal = nullptr;
	MsgId _originalId = 0;
	mutable Text _text = { 1 };
};

struct HistoryMessageReply : public BaseComponent<HistoryMessageReply> {
	HistoryMessageReply &operator=(HistoryMessageReply &&other) {
		replyToMsgId = other.replyToMsgId;
		std::swap(replyToMsg, other.replyToMsg);
		replyToLnk = std_::move(other.replyToLnk);
		replyToName = std_::move(other.replyToName);
		replyToText = std_::move(other.replyToText);
		replyToVersion = other.replyToVersion;
		_maxReplyWidth = other._maxReplyWidth;
		_replyToVia = std_::move(other._replyToVia);
		return *this;
	}
	~HistoryMessageReply() {
		// clearData() should be called by holder
		t_assert(replyToMsg == nullptr);
		t_assert(_replyToVia == nullptr);
	}

	bool updateData(HistoryMessage *holder, bool force = false);
	void clearData(HistoryMessage *holder); // must be called before destructor

	void checkNameUpdate() const;
	void updateName() const;
	void resize(int width) const;
	void itemRemoved(HistoryMessage *holder, HistoryItem *removed);

	enum PaintFlag {
		PaintInBubble = 0x01,
		PaintSelected = 0x02,
	};
	Q_DECLARE_FLAGS(PaintFlags, PaintFlag);
	void paint(Painter &p, const HistoryItem *holder, int x, int y, int w, PaintFlags flags) const;

	MsgId replyToId() const {
		return replyToMsgId;
	}
	int replyToWidth() const {
		return _maxReplyWidth;
	}
	ClickHandlerPtr replyToLink() const {
		return replyToLnk;
	}

	MsgId replyToMsgId = 0;
	HistoryItem *replyToMsg = nullptr;
	ClickHandlerPtr replyToLnk;
	mutable Text replyToName, replyToText;
	mutable int replyToVersion = 0;
	mutable int _maxReplyWidth = 0;
	std_::unique_ptr<HistoryMessageVia> _replyToVia;
	int toWidth = 0;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(HistoryMessageReply::PaintFlags);

class ReplyKeyboard;
struct HistoryMessageReplyMarkup : public BaseComponent<HistoryMessageReplyMarkup> {
	HistoryMessageReplyMarkup() = default;
	HistoryMessageReplyMarkup(MTPDreplyKeyboardMarkup::Flags f) : flags(f) {
	}

	void create(const MTPReplyMarkup &markup);

	struct Button {
		enum Type {
			Default,
			Url,
			Callback,
			RequestPhone,
			RequestLocation,
			SwitchInline,
		};
		Type type;
		QString text;
		QByteArray data;
		mutable mtpRequestId requestId;
	};
	using ButtonRow = QVector<Button>;
	using ButtonRows = QVector<ButtonRow>;

	ButtonRows rows;
	MTPDreplyKeyboardMarkup::Flags flags = 0;

	std_::unique_ptr<ReplyKeyboard> inlineKeyboard;

	// If >= 0 it holds the y coord of the inlineKeyboard before the last edition.
	int oldTop = -1;

private:
	void createFromButtonRows(const QVector<MTPKeyboardButtonRow> &v);

};

class ReplyMarkupClickHandler;
class ReplyKeyboard {
private:
	struct Button;

public:
	class Style {
	public:
		Style(const style::botKeyboardButton &st) : _st(&st) {
		}

		virtual void startPaint(Painter &p) const = 0;
		virtual style::font textFont() const = 0;

		int buttonSkip() const {
			return _st->margin;
		}
		int buttonPadding() const {
			return _st->padding;
		}
		int buttonHeight() const {
			return _st->height;
		}

		virtual void repaint(const HistoryItem *item) const = 0;
		virtual ~Style() {
		}

	protected:
		virtual void paintButtonBg(Painter &p, const QRect &rect, bool pressed, float64 howMuchOver) const = 0;
		virtual void paintButtonIcon(Painter &p, const QRect &rect, HistoryMessageReplyMarkup::Button::Type type) const = 0;
		virtual void paintButtonLoading(Painter &p, const QRect &rect) const = 0;
		virtual int minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const = 0;

	private:
		const style::botKeyboardButton *_st;

		void paintButton(Painter &p, const ReplyKeyboard::Button &button) const;
		friend class ReplyKeyboard;

	};
	typedef std_::unique_ptr<Style> StylePtr;

	ReplyKeyboard(const HistoryItem *item, StylePtr &&s);
	ReplyKeyboard(const ReplyKeyboard &other) = delete;
	ReplyKeyboard &operator=(const ReplyKeyboard &other) = delete;

	bool isEnoughSpace(int width, const style::botKeyboardButton &st) const;
	void setStyle(StylePtr &&s);
	void resize(int width, int height);

	// what width and height will best fit this keyboard
	int naturalWidth() const;
	int naturalHeight() const;

	void paint(Painter &p, const QRect &clip) const;
	ClickHandlerPtr getState(int x, int y) const;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active);
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed);

	void clearSelection();

private:
	const HistoryItem *_item;
	int _width = 0;

	friend class Style;
	using ReplyMarkupClickHandlerPtr = QSharedPointer<ReplyMarkupClickHandler>;
	struct Button {
		Text text = { 1 };
		QRect rect;
		int characters = 0;
		float64 howMuchOver = 0.;
		HistoryMessageReplyMarkup::Button::Type type;
		ReplyMarkupClickHandlerPtr link;
	};
	using ButtonRow = QVector<Button>;
	using ButtonRows = QVector<ButtonRow>;
	ButtonRows _rows;

	using Animations = QMap<int, uint64>;
	Animations _animations;
	Animation _a_selected;
	void step_selected(uint64 ms, bool timer);

	StylePtr _st;
};

class HistoryDependentItemCallback : public SharedCallback<void, ChannelData*, MsgId> {
public:
	HistoryDependentItemCallback(FullMsgId dependent) : _dependent(dependent) {
	}
	void call(ChannelData *channel, MsgId msgId) const override;

private:
	FullMsgId _dependent;

};

// any HistoryItem can have this Interface for
// displaying the day mark above the message
struct HistoryMessageDate : public BaseComponent<HistoryMessageDate> {
	void init(const QDateTime &date);

	int height() const;
	void paint(Painter &p, int y, int w) const;

	QString _text;
	int _width = 0;
};

// any HistoryItem can have this Interface for
// displaying the unread messages bar above the message
struct HistoryMessageUnreadBar : public BaseComponent<HistoryMessageUnreadBar> {
	void init(int count);

	static int height();
	static int marginTop();

	void paint(Painter &p, int y, int w) const;

	QString _text;
	int _width = 0;

	// if unread bar is freezed the new messages do not
	// increment the counter displayed by this bar
	//
	// it happens when we've opened the conversation and
	// we've seen the bar and new messages are marked as read
	// as soon as they are added to the chat history
	bool _freezed = false;
};

// HistoryMedia has a special owning smart pointer
// which regs/unregs this media to the holding HistoryItem
class HistoryMedia;
class HistoryMediaPtr {
public:
	HistoryMediaPtr() = default;
	HistoryMediaPtr(const HistoryMediaPtr &other) = delete;
	HistoryMediaPtr &operator=(const HistoryMediaPtr &other) = delete;
	HistoryMedia *data() const {
		return _p;
	}
	void reset(HistoryMedia *p = nullptr);
	void clear() {
		reset();
	}
	bool isNull() const {
		return data() == nullptr;
	}

	HistoryMedia *operator->() const {
		return data();
	}
	HistoryMedia &operator*() const {
		t_assert(!isNull());
		return *data();
	}
	explicit operator bool() const {
		return !isNull();
	}
	~HistoryMediaPtr() {
		clear();
	}

private:
	HistoryMedia *_p = nullptr;

};


namespace internal {

TextSelection unshiftSelection(TextSelection selection, const Text &byText);
TextSelection shiftSelection(TextSelection selection, const Text &byText);

} // namespace internal

class HistoryItem : public HistoryElem, public Composer, public ClickHandlerHost {
public:

	HistoryItem(const HistoryItem &) = delete;
	HistoryItem &operator=(const HistoryItem &) = delete;

	int resizeGetHeight(int width) {
		if (_flags & MTPDmessage_ClientFlag::f_pending_init_dimensions) {
			_flags &= ~MTPDmessage_ClientFlag::f_pending_init_dimensions;
			initDimensions();
		}
		if (_flags & MTPDmessage_ClientFlag::f_pending_resize) {
			_flags &= ~MTPDmessage_ClientFlag::f_pending_resize;
		}
		return resizeGetHeight_(width);
	}
	virtual void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const = 0;

	virtual void dependencyItemRemoved(HistoryItem *dependency) {
	}
	virtual bool updateDependencyItem() {
		return true;
	}
	virtual MsgId dependencyMsgId() const {
		return 0;
	}
	virtual bool notificationReady() const {
		return true;
	}

	UserData *viaBot() const {
		if (const HistoryMessageVia *via = Get<HistoryMessageVia>()) {
			return via->_bot;
		}
		return nullptr;
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
	void attachToBlock(HistoryBlock *block, int index) {
		t_assert(_block == nullptr);
		t_assert(_indexInBlock < 0);
		t_assert(block != nullptr);
		t_assert(index >= 0);

		_block = block;
		_indexInBlock = index;
		if (pendingResize()) {
			_history->setHasPendingResizedItems();
		}
	}
	void setIndexInBlock(int index) {
		t_assert(_block != nullptr);
		t_assert(index >= 0);

		_indexInBlock = index;
	}
	int indexInBlock() const {
		if (_indexInBlock >= 0) {
			t_assert(_block != nullptr);
			t_assert(_block->items.at(_indexInBlock) == this);
		} else if (_block != nullptr) {
			t_assert(_indexInBlock >= 0);
			t_assert(_block->items.at(_indexInBlock) == this);
		}
		return _indexInBlock;
	}
	bool out() const {
		return _flags & MTPDmessage::Flag::f_out;
	}
	bool unread() const {
		if (out() && id > 0 && id < _history->outboxReadBefore) return false;
		if (!out() && id > 0) {
			if (id < _history->inboxReadBefore) return false;
			if (channelId() != NoChannel) return true; // no unread flag for incoming messages in channels
		}
		if (history()->peer->isSelf()) return false; // messages from myself are always read
		if (out() && history()->peer->migrateTo()) return false; // outgoing messages in converted chats are always read
		return (_flags & MTPDmessage::Flag::f_unread);
	}
	bool mentionsMe() const {
		return _flags & MTPDmessage::Flag::f_mentioned;
	}
	bool isMediaUnread() const {
		return (_flags & MTPDmessage::Flag::f_media_unread) && (channelId() == NoChannel);
	}
	void markMediaRead() {
		_flags &= ~MTPDmessage::Flag::f_media_unread;
	}
	bool definesReplyKeyboard() const {
		if (auto markup = Get<HistoryMessageReplyMarkup>()) {
			if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
				return false;
			}
			return true;
		}

		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		return (_flags & MTPDmessage::Flag::f_reply_markup);
	}
	MTPDreplyKeyboardMarkup::Flags replyKeyboardFlags() const {
		t_assert(definesReplyKeyboard());
		if (auto markup = Get<HistoryMessageReplyMarkup>()) {
			return markup->flags;
		}

		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		return qFlags(MTPDreplyKeyboardMarkup_ClientFlag::f_zero);
	}
	bool hasSwitchInlineButton() const {
		return _flags & MTPDmessage_ClientFlag::f_has_switch_inline_button;
	}
	bool hasTextLinks() const {
		return _flags & MTPDmessage_ClientFlag::f_has_text_links;
	}
	bool isGroupMigrate() const {
		return _flags & MTPDmessage_ClientFlag::f_is_group_migrate;
	}
	bool hasViews() const {
		return _flags & MTPDmessage::Flag::f_views;
	}
	bool isPost() const {
		return _flags & MTPDmessage::Flag::f_post;
	}
	bool isImportant() const {
		return _history->isChannel() && isImportantChannelMessage(id, _flags);
	}
	bool indexInOverview() const {
		return (id > 0) && (!history()->isChannel() || history()->isMegagroup() || isPost());
	}
	bool isSilent() const {
		return _flags & MTPDmessage::Flag::f_silent;
	}
	bool hasOutLayout() const {
		return out() && !isPost();
	}
	virtual int32 viewsCount() const {
		return hasViews() ? 1 : -1;
	}

	virtual bool needCheck() const {
		return out() || (id < 0 && history()->peer->isSelf());
	}
	virtual bool hasPoint(int x, int y) const {
		return false;
	}

	virtual HistoryTextState getState(int x, int y, HistoryStateRequest request) const = 0;

	virtual TextSelection adjustSelection(TextSelection selection, TextSelectType type) const {
		return selection;
	}

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	virtual HistoryItemType type() const {
		return HistoryItemMsg;
	}
	virtual bool serviceMsg() const {
		return false;
	}
	virtual void applyEdition(const MTPDmessage &message) {
	}
	virtual void updateMedia(const MTPMessageMedia *media) {
	}
	virtual int32 addToOverview(AddToOverviewMethod method) {
		return 0;
	}
	virtual bool hasBubble() const {
		return false;
	}
	virtual void previousItemChanged();

	virtual QString selectedText(TextSelection selection) const {
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
	virtual void setViewsCount(int32 count) {
	}
	virtual void setId(MsgId newId);
	virtual void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const = 0;
    virtual QString notificationHeader() const {
        return QString();
    }
    virtual QString notificationText() const = 0;

	bool canDelete() const {
		ChannelData *channel = _history->peer->asChannel();
		if (!channel) return !(_flags & MTPDmessage_ClientFlag::f_is_group_migrate);

		if (id == 1) return false;
		if (channel->amCreator()) return true;
		if (isPost()) {
			if (channel->amEditor() && out()) return true;
			return false;
		}
		return (channel->amEditor() || channel->amModerator() || out());
	}

	bool canPin() const {
		return id > 0 && _history->peer->isMegagroup() && (_history->peer->asChannel()->amEditor() || _history->peer->asChannel()->amCreator()) && toHistoryMessage();
	}

	bool canEdit(const QDateTime &cur) const;
	bool wasEdited() const {
		return _flags & MTPDmessage::Flag::f_edit_date;
	}

	bool suggestBanReportDeleteAll() const {
		ChannelData *channel = history()->peer->asChannel();
		if (!channel || (!channel->amEditor() && !channel->amCreator())) return false;
		return !isPost() && !out() && from()->isUser() && toHistoryMessage();
	}

	bool hasDirectLink() const {
		return id > 0 && _history->peer->isChannel() && _history->peer->asChannel()->isPublic() && !_history->peer->isMegagroup();
	}
	QString directLink() const {
		return hasDirectLink() ? qsl("https://telegram.me/") + _history->peer->asChannel()->username + '/' + QString::number(id) : QString();
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

	virtual HistoryMedia *getMedia() const {
		return nullptr;
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

	virtual int infoWidth() const {
		return 0;
	}
	virtual int timeLeft() const {
		return 0;
	}
	virtual int timeWidth() const {
		return 0;
	}
	virtual bool pointInTime(int32 right, int32 bottom, int x, int y, InfoDisplayType type) const {
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
		return nullptr;
	}
	virtual const HistoryMessage *toHistoryMessage() const { // dynamic_cast optimize
		return nullptr;
	}
	MsgId replyToId() const {
		if (auto reply = Get<HistoryMessageReply>()) {
			return reply->replyToId();
		}
		return 0;
	}

	bool hasFromName() const {
		return (!out() || isPost()) && !history()->peer->isUser();
	}
	PeerData *author() const {
		return isPost() ? history()->peer : _from;
	}

	PeerData *fromOriginal() const {
		if (const HistoryMessageForwarded *fwd = Get<HistoryMessageForwarded>()) {
			return fwd->_fromOriginal;
		}
		return from();
	}
	PeerData *authorOriginal() const {
		if (const HistoryMessageForwarded *fwd = Get<HistoryMessageForwarded>()) {
			return fwd->_authorOriginal;
		}
		return author();
	}

	// count > 0 - creates the unread bar if necessary and
	// sets unread messages count if bar is not freezed yet
	// count <= 0 - destroys the unread bar
	void setUnreadBarCount(int count);
	void destroyUnreadBar();

	// marks the unread bar as freezed so that unread
	// messages count will not change for this bar
	// when the new messages arrive in this chat history
	void setUnreadBarFreezed();

	bool pendingResize() const {
		return _flags & MTPDmessage_ClientFlag::f_pending_resize;
	}
	void setPendingResize() {
		_flags |= MTPDmessage_ClientFlag::f_pending_resize;
		if (!detached()) {
			_history->setHasPendingResizedItems();
		}
	}
	bool pendingInitDimensions() const {
		return _flags & MTPDmessage_ClientFlag::f_pending_init_dimensions;
	}
	void setPendingInitDimensions() {
		_flags |= MTPDmessage_ClientFlag::f_pending_init_dimensions;
		setPendingResize();
	}

	int displayedDateHeight() const {
		if (auto date = Get<HistoryMessageDate>()) {
			return date->height();
		}
		return 0;
	}
	int marginTop() const {
		int result = 0;
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
		result += displayedDateHeight();
		if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
			result += unreadbar->height();
		}
		return result;
	}
	int marginBottom() const {
		return st::msgMargin.bottom();
	}
	bool isAttachedToPrevious() const {
		return _flags & MTPDmessage_ClientFlag::f_attach_to_previous;
	}

	void clipCallback(ClipReaderNotification notification);

	virtual ~HistoryItem();

protected:

	HistoryItem(History *history, MsgId msgId, MTPDmessage::Flags flags, QDateTime msgDate, int32 from);

	// to completely create history item we need to call
	// a virtual method, it can not be done from constructor
	virtual void finishCreate();

	// called from resizeGetHeight() when MTPDmessage_ClientFlag::f_pending_init_dimensions is set
	virtual void initDimensions() = 0;

	virtual int resizeGetHeight_(int width) = 0;

	PeerData *_from;
	History *_history;
	HistoryBlock *_block = nullptr;
	int _indexInBlock = -1;
	MTPDmessage::Flags _flags;

	mutable int32 _authorNameVersion;

	HistoryItem *previous() const {
		if (_block && _indexInBlock >= 0) {
			if (_indexInBlock > 0) {
				return _block->items.at(_indexInBlock - 1);
			}
			if (HistoryBlock *previousBlock = _block->previous()) {
				t_assert(!previousBlock->items.isEmpty());
				return previousBlock->items.back();
			}
		}
		return nullptr;
	}

	// this should be used only in previousItemChanged()
	// to add required bits to the Composer mask
	// after that always use Has<HistoryMessageDate>()
	bool displayDate() const {
		if (HistoryItem *prev = previous()) {
			return prev->date.date().day() != date.date().day();
		}
		return true;
	}

	// this should be used only in previousItemChanged() or when
	// HistoryMessageDate or HistoryMessageUnreadBar bit is changed in the Composer mask
	// then the result should be cached in a client side flag MTPDmessage_ClientFlag::f_attach_to_previous
	void recountAttachToPrevious();

	const HistoryMessageReplyMarkup *inlineReplyMarkup() const {
		if (auto markup = Get<HistoryMessageReplyMarkup>()) {
			if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
				return markup;
			}
		}
		return nullptr;
	}
	const ReplyKeyboard *inlineReplyKeyboard() const {
		if (auto markup = inlineReplyMarkup()) {
			return markup->inlineKeyboard.get();
		}
		return nullptr;
	}
	HistoryMessageReplyMarkup *inlineReplyMarkup() {
		return const_cast<HistoryMessageReplyMarkup*>(static_cast<const HistoryItem*>(this)->inlineReplyMarkup());
	}
	ReplyKeyboard *inlineReplyKeyboard() {
		return const_cast<ReplyKeyboard*>(static_cast<const HistoryItem*>(this)->inlineReplyKeyboard());
	}

	TextSelection toMediaSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _text);
	}
	TextSelection fromMediaSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _text);
	}

	Text _text = { int(st::msgMinWidth) };
	int32 _textWidth, _textHeight;

	HistoryMediaPtr _media;

};

// make all the constructors in HistoryItem children protected
// and wrapped with a static create() call with the same args
// so that history item can not be created directly, without
// calling a virtual finishCreate() method
template <typename T>
class HistoryItemInstantiated {
public:
	template <typename ... Args>
	static T *_create(Args ... args) {
		T *result = new T(args ...);
		result->finishCreate();
		return result;
	}
};

class MessageClickHandler : public LeftButtonClickHandler {
public:
	MessageClickHandler(PeerId peer, MsgId msgid) : _peer(peer), _msgid(msgid) {
	}
	MessageClickHandler(HistoryItem *item) : _peer(item->history()->peer->id), _msgid(item->id) {
	}
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

class GoToMessageClickHandler : public MessageClickHandler {
public:
	using MessageClickHandler::MessageClickHandler;
protected:
	void onClickImpl() const override;
};

class CommentsClickHandler : public MessageClickHandler {
public:
	using MessageClickHandler::MessageClickHandler;
protected:
	void onClickImpl() const override;
};

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
	HistoryMedia(HistoryItem *parent) : _parent(parent) {
	}
	HistoryMedia(const HistoryMedia &other) = delete;
	HistoryMedia &operator=(const HistoryMedia &other) = delete;

	virtual HistoryMediaType type() const = 0;
	virtual QString inDialogsText() const = 0;
	virtual QString selectedText(TextSelection selection) const = 0;

	bool hasPoint(int x, int y) const {
		return (x >= 0 && y >= 0 && x < _width && y < _height);
	}

	virtual bool isDisplayed() const {
		return true;
	}
	virtual bool hasTextForCopy() const {
		return false;
	}
	virtual void initDimensions() = 0;
	virtual int resizeGetHeight(int width) {
		_width = qMin(width, _maxw);
		return _height;
	}
	virtual void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const = 0;
	virtual HistoryTextState getState(int x, int y, HistoryStateRequest request) const = 0;

	// if we are in selecting items mode perhaps we want to
	// toggle selection instead of activating the pressed link
	virtual bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const = 0;

	// if we press and drag on this media should we drag the item
	virtual bool dragItem() const {
		return false;
	}

	virtual TextSelection adjustSelection(TextSelection selection, TextSelectType type) const {
		return selection;
	}

	// if we press and drag this link should we drag the item
	virtual bool dragItemByHandler(const ClickHandlerPtr &p) const = 0;

	virtual void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	}
	virtual void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	}

	virtual bool uploading() const {
		return false;
	}
	virtual HistoryMedia *clone(HistoryItem *newParent) const = 0;

	virtual DocumentData *getDocument() {
		return nullptr;
	}
	virtual ClipReader *getClipReader() {
		return nullptr;
	}

	bool playInline(/*bool autoplay = false*/) {
		return playInline(false);
	}
	virtual bool playInline(bool autoplay) {
		return false;
	}
	virtual void stopInline() {
	}

	virtual void attachToParent() {
	}

	virtual void detachFromParent() {
	}

	virtual void updateSentMedia(const MTPMessageMedia &media) {
	}

	// After sending an inline result we may want to completely recreate
	// the media (all media that was generated on client side, for example)
	virtual bool needReSetInlineResultMedia(const MTPMessageMedia &media) {
		return true;
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
	virtual bool needsBubble() const = 0;
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

	int currentWidth() const {
		return _width;
	}

protected:
	HistoryItem *_parent;
	int _width = 0;

};

inline MediaOverviewType mediaToOverviewType(HistoryMedia *media) {
	switch (media->type()) {
	case MediaTypePhoto: return OverviewPhotos;
	case MediaTypeVideo: return OverviewVideos;
	case MediaTypeFile: return OverviewFiles;
	case MediaTypeMusicFile: return media->getDocument()->isMusic() ? OverviewMusicFiles : OverviewFiles;
	case MediaTypeVoiceFile: return OverviewVoiceFiles;
	case MediaTypeGif: return media->getDocument()->isGifv() ? OverviewCount : OverviewFiles;
	default: break;
	}
	return OverviewCount;
}

class HistoryFileMedia : public HistoryMedia {
public:
	using HistoryMedia::HistoryMedia;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return p == _openl || p == _savel || p == _cancell;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return p == _openl || p == _savel || p == _cancell;
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	~HistoryFileMedia();

protected:
	ClickHandlerPtr _openl, _savel, _cancell;
	void setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell);
	void setDocumentLinks(DocumentData *document, bool inlinegif = false) {
		ClickHandlerPtr open, save;
		if (inlinegif) {
			open.reset(new GifOpenClickHandler(document));
		} else {
			open.reset(new DocumentOpenClickHandler(document));
		}
		if (inlinegif) {
			save.reset(new GifOpenClickHandler(document));
		} else if (document->voice()) {
			save.reset(new DocumentOpenClickHandler(document));
		} else {
			save.reset(new DocumentSaveClickHandler(document));
		}
		setLinks(std_::move(open), std_::move(save), MakeShared<DocumentCancelClickHandler>(document));
	}

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int32 _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const;

	void step_thumbOver(float64 ms, bool timer);
	void step_radial(uint64 ms, bool timer);

	void ensureAnimation() const;
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
	mutable AnimationData *_animation = nullptr;

};

class HistoryPhoto : public HistoryFileMedia {
public:
	HistoryPhoto(HistoryItem *parent, PhotoData *photo, const QString &caption);
	HistoryPhoto(HistoryItem *parent, PeerData *chat, const MTPDphoto &photo, int width);
	HistoryPhoto(HistoryItem *parent, const HistoryPhoto &other);

	void init();
	HistoryMediaType type() const override {
		return MediaTypePhoto;
	}
	HistoryPhoto *clone(HistoryItem *newParent) const override {
		return new HistoryPhoto(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	PhotoData *photo() const {
		return _data;
	}

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	void attachToParent() override;
	void detachFromParent() override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	QString getCaption() const override {
		return _caption.original();
	}
	bool needsBubble() const override {
		if (!_caption.isEmpty()) {
			return true;
		}
		if (_parent->viaBot()) {
			return true;
		}
		return (_parent->Has<HistoryMessageForwarded>() || _parent->Has<HistoryMessageReply>());
	}
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool hideFromName() const override {
		return true;
	}

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}

private:
	PhotoData *_data;
	int16 _pixw = 1;
	int16 _pixh = 1;
	Text _caption;

};

class HistoryVideo : public HistoryFileMedia {
public:
	HistoryVideo(HistoryItem *parent, DocumentData *document, const QString &caption);
	HistoryVideo(HistoryItem *parent, const HistoryVideo &other);
	HistoryMediaType type() const override {
		return MediaTypeVideo;
	}
	HistoryVideo *clone(HistoryItem *newParent) const override {
		return new HistoryVideo(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	DocumentData *getDocument() override {
		return _data;
	}

	bool uploading() const override {
		return _data->uploading();
	}

	void attachToParent() override;
	void detachFromParent() override;

	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	QString getCaption() const override {
		return _caption.original();
	}
	bool needsBubble() const override {
		if (!_caption.isEmpty()) {
			return true;
		}
		if (_parent->viaBot()) {
			return true;
		}
		return (_parent->Has<HistoryMessageForwarded>() || _parent->Has<HistoryMessageReply>());
	}
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool hideFromName() const override {
		return true;
	}

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}

private:
	DocumentData *_data;
	int32 _thumbw;
	Text _caption;

	void setStatusSize(int32 newSize) const;
	void updateStatusText() const;

};

struct HistoryDocumentThumbed : public BaseComponent<HistoryDocumentThumbed> {
	ClickHandlerPtr _linksavel, _linkcancell;
	int _thumbw = 0;

	mutable int _linkw = 0;
	mutable QString _link;
};
struct HistoryDocumentCaptioned : public BaseComponent<HistoryDocumentCaptioned> {
	Text _caption = { int(st::msgFileMinWidth) - st::msgPadding.left() - st::msgPadding.right() };
};
struct HistoryDocumentNamed : public BaseComponent<HistoryDocumentNamed> {
	QString _name;
	int _namew = 0;
};
class HistoryDocument;
struct HistoryDocumentVoicePlayback {
	HistoryDocumentVoicePlayback(const HistoryDocument *that);

	int32 _position;
	anim::fvalue a_progress;
	Animation _a_progress;
};
struct HistoryDocumentVoice : public BaseComponent<HistoryDocumentVoice> {
	HistoryDocumentVoice &operator=(HistoryDocumentVoice &&other) {
		std::swap(_playback, other._playback);
		return *this;
	}
	~HistoryDocumentVoice() {
		deleteAndMark(_playback);
	}
	void ensurePlayback(const HistoryDocument *interfaces) const;
	void checkPlaybackFinished() const;
	mutable HistoryDocumentVoicePlayback *_playback = nullptr;
};

class HistoryDocument : public HistoryFileMedia, public Composer {
public:
	HistoryDocument(HistoryItem *parent, DocumentData *document, const QString &caption);
	HistoryDocument(HistoryItem *parent, const HistoryDocument &other);
	HistoryMediaType type() const override {
		return _data->voice() ? MediaTypeVoiceFile : (_data->song() ? MediaTypeMusicFile : MediaTypeFile);
	}
	HistoryDocument *clone(HistoryItem *newParent) const override {
		return new HistoryDocument(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		if (auto captioned = Get<HistoryDocumentCaptioned>()) {
			return captioned->_caption.adjustSelection(selection, type);
		}
		return selection;
	}
	bool hasTextForCopy() const override {
		return Has<HistoryDocumentCaptioned>();
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	bool uploading() const override {
		return _data->uploading();
	}

	DocumentData *getDocument() override {
		return _data;
	}

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	QString getCaption() const override {
		if (const HistoryDocumentCaptioned *captioned = Get<HistoryDocumentCaptioned>()) {
			return captioned->_caption.original();
		}
		return QString();
	}
	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}
	QMargins bubbleMargins() const override {
		return Get<HistoryDocumentThumbed>() ? QMargins(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbPadding.left(), st::msgFileThumbPadding.bottom()) : st::msgPadding;
	}
	bool hideForwardedFrom() const override {
		return _data->song();
	}

	void step_voiceProgress(float64 ms, bool timer);

protected:
	float64 dataProgress() const override {
		return _data->progress();
	}
	bool dataFinished() const override {
		return !_data->loading() && !_data->uploading();
	}
	bool dataLoaded() const override {
		return _data->loaded();
	}

private:
	void createComponents(bool caption);
	DocumentData *_data;

	void setStatusSize(int32 newSize, qint64 realDuration = 0) const;
	bool updateStatusText() const; // returns showPause

};

class HistoryGif : public HistoryFileMedia {
public:
	HistoryGif(HistoryItem *parent, DocumentData *document, const QString &caption);
	HistoryGif(HistoryItem *parent, const HistoryGif &other);
	HistoryMediaType type() const override {
		return MediaTypeGif;
	}
	HistoryGif *clone(HistoryItem *newParent) const override {
		return new HistoryGif(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _caption.adjustSelection(selection, type);
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	bool uploading() const override {
		return _data->uploading();
	}

	DocumentData *getDocument() override {
		return _data;
	}
	ClipReader *getClipReader() override {
		return gif();
	}

	bool playInline(bool autoplay) override;
	void stopInline() override;

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool hasReplyPreview() const override {
		return !_data->thumb->isNull();
	}
	ImagePtr replyPreview() override;

	QString getCaption() const override {
		return _caption.original();
	}
	bool needsBubble() const override {
		if (!_caption.isEmpty()) {
			return true;
		}
		if (_parent->viaBot()) {
			return true;
		}
		return (_parent->Has<HistoryMessageForwarded>() || _parent->Has<HistoryMessageReply>());
	}
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool hideFromName() const override {
		return true;
	}

	~HistoryGif();

protected:
	float64 dataProgress() const override;
	bool dataFinished() const override;
	bool dataLoaded() const override;

private:
	DocumentData *_data;
	int32 _thumbw, _thumbh;
	Text _caption;

	ClipReader *_gif;
	ClipReader *gif() {
		return (_gif == BadClipReader) ? nullptr : _gif;
	}
	const ClipReader *gif() const {
		return (_gif == BadClipReader) ? nullptr : _gif;
	}

	void setStatusSize(int32 newSize) const;
	void updateStatusText() const;

};

class HistorySticker : public HistoryMedia {
public:
	HistorySticker(HistoryItem *parent, DocumentData *document);
	HistoryMediaType type() const override {
		return MediaTypeSticker;
	}
	HistorySticker *clone(HistoryItem *newParent) const override {
		return new HistorySticker(newParent, _data);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItem() const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	DocumentData *getDocument() override {
		return _data;
	}

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;
	bool needReSetInlineResultMedia(const MTPMessageMedia &media) override;

	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return true;
	}

private:
	int additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const;
	int additionalWidth() const {
		return additionalWidth(_parent->Get<HistoryMessageVia>(), _parent->Get<HistoryMessageReply>());
	}

	int16 _pixw, _pixh;
	ClickHandlerPtr _packLink;
	DocumentData *_data;
	QString _emoji;

};

class SendMessageClickHandler : public PeerClickHandler {
public:
	using PeerClickHandler::PeerClickHandler;
protected:
	void onClickImpl() const override;
};

class AddContactClickHandler : public MessageClickHandler {
public:
	using MessageClickHandler::MessageClickHandler;
protected:
	void onClickImpl() const override;
};

class HistoryContact : public HistoryMedia {
public:
	HistoryContact(HistoryItem *parent, int32 userId, const QString &first, const QString &last, const QString &phone);
	HistoryMediaType type() const override {
		return MediaTypeContact;
	}
	HistoryContact *clone(HistoryItem *newParent) const override {
		return new HistoryContact(newParent, _userId, _fname, _lname, _phone);
	}

	void initDimensions() override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	void attachToParent() override;
	void detachFromParent() override;

	void updateSentMedia(const MTPMessageMedia &media) override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
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

	ClickHandlerPtr _linkl;
	int32 _linkw;
	QString _link;
};

class HistoryWebPage : public HistoryMedia {
public:
	HistoryWebPage(HistoryItem *parent, WebPageData *data);
	HistoryWebPage(HistoryItem *parent, const HistoryWebPage &other);
	HistoryMediaType type() const override {
		return MediaTypeWebPage;
	}
	HistoryWebPage *clone(HistoryItem *newParent) const override {
		return new HistoryWebPage(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;
	bool hasTextForCopy() const override {
		return false; // we do not add _title and _description in FullSelection text copy.
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return _attach && _attach->toggleSelectionByHandlerClick(p);
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return _attach && _attach->dragItemByHandler(p);
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	bool isDisplayed() const override {
		return !_data->pendingTill;
	}
	DocumentData *getDocument() override {
		return _attach ? _attach->getDocument() : 0;
	}
	ClipReader *getClipReader() override {
		return _attach ? _attach->getClipReader() : 0;
	}
	bool playInline(bool autoplay) override {
		return _attach ? _attach->playInline(autoplay) : false;
	}
	void stopInline() override {
		if (_attach) _attach->stopInline();
	}

	void attachToParent() override;
	void detachFromParent() override;

	bool hasReplyPreview() const override {
		return (_data->photo && !_data->photo->thumb->isNull()) || (_data->document && !_data->document->thumb->isNull());
	}
	ImagePtr replyPreview() override;

	WebPageData *webpage() {
		return _data;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	HistoryMedia *attach() const {
		return _attach;
	}

	~HistoryWebPage();

private:
	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}

	WebPageData *_data;
	ClickHandlerPtr _openl;
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

struct LocationCoords;
struct LocationData;
class LocationManager : public QObject {
	Q_OBJECT
public:
	LocationManager() : manager(0), black(0) {
	}
	void init();
	void reinit();
	void deinit();

	void getData(LocationData *data);

	~LocationManager() {
		deinit();
	}

public slots:
	void onFinished(QNetworkReply *reply);
	void onFailed(QNetworkReply *reply);

private:
	void failed(LocationData *data);

	QNetworkAccessManager *manager;
	QMap<QNetworkReply*, LocationData*> dataLoadings, imageLoadings;
	QMap<LocationData*, int32> serverRedirects;
	ImagePtr *black;
};

class HistoryLocation : public HistoryMedia {
public:
	HistoryLocation(HistoryItem *parent, const LocationCoords &coords, const QString &title = QString(), const QString &description = QString());
	HistoryLocation(HistoryItem *parent, const HistoryLocation &other);
	HistoryMediaType type() const override {
		return MediaTypeLocation;
	}
	HistoryLocation *clone(HistoryItem *newParent) const override {
		return new HistoryLocation(newParent, *this);
	}

	void initDimensions() override;
	int resizeGetHeight(int32 width) override;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;
	bool hasTextForCopy() const override {
		return !_title.isEmpty() || !_description.isEmpty();
	}

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return p == _link;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return p == _link;
	}

	QString inDialogsText() const override;
	QString selectedText(TextSelection selection) const override;

	bool needsBubble() const override {
		if (!_title.isEmpty() || !_description.isEmpty()) {
			return true;
		}
		if (_parent->viaBot()) {
			return true;
		}
		return (_parent->Has<HistoryMessageForwarded>() || _parent->Has<HistoryMessageReply>());
	}
	bool customInfoLayout() const override {
		return true;
	}

private:
	TextSelection toDescriptionSelection(TextSelection selection) const {
		return internal::unshiftSelection(selection, _title);
	}
	TextSelection fromDescriptionSelection(TextSelection selection) const {
		return internal::shiftSelection(selection, _title);
	}

	LocationData *_data;
	Text _title, _description;
	ClickHandlerPtr _link;

	int32 fullWidth() const;
	int32 fullHeight() const;

};

class ViaInlineBotClickHandler : public LeftButtonClickHandler {
public:
	ViaInlineBotClickHandler(UserData *bot) : _bot(bot) {
	}

protected:
	void onClickImpl() const override;

private:
	UserData *_bot;

};

class HistoryMessage : public HistoryItem, private HistoryItemInstantiated<HistoryMessage> {
public:

	static HistoryMessage *create(History *history, const MTPDmessage &msg) {
		return _create(history, msg);
	}
	static HistoryMessage *create(History *history, MsgId msgId, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *fwd) {
		return _create(history, msgId, flags, date, from, fwd);
	}
	static HistoryMessage *create(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, const QString &msg, const EntitiesInText &entities) {
		return _create(history, msgId, flags, replyTo, viaBotId, date, from, msg, entities);
	}
	static HistoryMessage *create(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
		return _create(history, msgId, flags, replyTo, viaBotId, date, from, doc, caption, markup);
	}
	static HistoryMessage *create(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
		return _create(history, msgId, flags, replyTo, viaBotId, date, from, photo, caption, markup);
	}

	void initTime();
	void initMedia(const MTPMessageMedia *media, QString &currentText);
	void initMediaFromDocument(DocumentData *doc, const QString &caption);
	void fromNameUpdated(int32 width) const;

	int32 plainMaxWidth() const;
	void countPositionAndSize(int32 &left, int32 &width) const;

	bool emptyText() const {
		return _text.isEmpty();
	}
	bool drawBubble() const {
		return _media ? (!emptyText() || _media->needsBubble()) : true;
	}
	bool hasBubble() const override {
		return drawBubble();
	}
	bool displayFromName() const {
		if (!hasFromName()) return false;
		if (isAttachedToPrevious()) return false;

		return (!emptyText() || !_media || !_media->isDisplayed() || Has<HistoryMessageReply>() || Has<HistoryMessageForwarded>() || viaBot() || !_media->hideFromName());
	}
	bool uploading() const {
		return _media && _media->uploading();
	}

	void drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const override;
	void setViewsCount(int32 count) override;
	void setId(MsgId newId) override;
	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;

	void dependencyItemRemoved(HistoryItem *dependency) override;

	void destroy() override;

	bool hasPoint(int x, int y) const override;
	bool pointInTime(int32 right, int32 bottom, int x, int y, InfoDisplayType type) const override;

	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override {
		if (_media) _media->clickHandlerActiveChanged(p, active);
		HistoryItem::clickHandlerActiveChanged(p, active);
	}
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override {
		if (_media) _media->clickHandlerPressedChanged(p, pressed);
		HistoryItem::clickHandlerPressedChanged(p, pressed);
	}

	void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const override;
    QString notificationHeader() const override;
    QString notificationText() const override;

	void applyEdition(const MTPDmessage &message) override;
	void updateMedia(const MTPMessageMedia *media) override;
	int32 addToOverview(AddToOverviewMethod method) override;
	void eraseFromOverview();

	QString selectedText(TextSelection selection) const override;
	QString inDialogsText() const override;
	HistoryMedia *getMedia() const override;
	void setText(const QString &text, const EntitiesInText &entities) override;
	QString originalText() const override;
	EntitiesInText originalEntities() const override;
	bool textHasLinks() override;

	int32 infoWidth() const override {
		int32 result = _timeWidth;
		if (const HistoryMessageViews *views = Get<HistoryMessageViews>()) {
			result += st::msgDateViewsSpace + views->_viewsWidth + st::msgDateCheckSpace + st::msgViewsImg.pxWidth();
		} else if (id < 0 && history()->peer->isSelf()) {
			result += st::msgDateCheckSpace + st::msgCheckImg.pxWidth();
		}
		if (out() && !isPost()) {
			result += st::msgDateCheckSpace + st::msgCheckImg.pxWidth();
		}
		return result;
	}
	int32 timeLeft() const override {
		int32 result = 0;
		if (const HistoryMessageViews *views = Get<HistoryMessageViews>()) {
			result += st::msgDateViewsSpace + views->_viewsWidth + st::msgDateCheckSpace + st::msgViewsImg.pxWidth();
		} else if (id < 0 && history()->peer->isSelf()) {
			result += st::msgDateCheckSpace + st::msgCheckImg.pxWidth();
		}
		return result;
	}
	int32 timeWidth() const override {
		return _timeWidth;
	}

	int32 viewsCount() const override {
		if (const HistoryMessageViews *views = Get<HistoryMessageViews>()) {
			return views->_views;
		}
		return HistoryItem::viewsCount();
	}

	bool updateDependencyItem() override {
		if (auto reply = Get<HistoryMessageReply>()) {
			return reply->updateData(this, true);
		}
		return true;
	}
	MsgId dependencyMsgId() const override {
		return replyToId();
	}

	HistoryMessage *toHistoryMessage() override { // dynamic_cast optimize
		return this;
	}
	const HistoryMessage *toHistoryMessage() const override { // dynamic_cast optimize
		return this;
	}

	// hasFromPhoto() returns true even if we don't display the photo
	// but we need to skip a place at the left side for this photo
	bool displayFromPhoto() const;
	bool hasFromPhoto() const;

	~HistoryMessage();

private:

	HistoryMessage(History *history, const MTPDmessage &msg);
	HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *fwd); // local forwarded
	HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, const QString &msg, const EntitiesInText &entities); // local message
	HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup); // local document
	HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup); // local photo
	friend class HistoryItemInstantiated<HistoryMessage>;

	void initDimensions() override;
	int resizeGetHeight_(int width) override;
	int performResizeGetHeight(int width);

	bool displayForwardedFrom() const {
		if (const HistoryMessageForwarded *fwd = Get<HistoryMessageForwarded>()) {
			return Has<HistoryMessageVia>() || !_media || !_media->isDisplayed() || fwd->_authorOriginal->isChannel() || !_media->hideForwardedFrom();
		}
		return false;
	}

	void paintForwardedInfo(Painter &p, QRect &trect, bool selected) const;
	void paintReplyInfo(Painter &p, QRect &trect, bool selected) const;

	// this method draws "via @bot" if it is not painted in forwarded info or in from name
	void paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const;

	void setMedia(const MTPMessageMedia *media);
	void setReplyMarkup(const MTPReplyMarkup *markup);

	QString _timeText;
	int _timeWidth = 0;

	struct CreateConfig {
		MsgId replyTo = 0;
		UserId viaBotId = 0;
		int viewsCount = -1;
		PeerId authorIdOriginal = 0;
		PeerId fromIdOriginal = 0;
		MsgId originalId = 0;
		QDateTime editDate;
		const MTPReplyMarkup *markup = nullptr;
	};
	void createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, const MTPReplyMarkup &markup);
	void createComponents(const CreateConfig &config);

	class KeyboardStyle : public ReplyKeyboard::Style {
	public:
		using ReplyKeyboard::Style::Style;

		void startPaint(Painter &p) const override;
		style::font textFont() const override;
		void repaint(const HistoryItem *item) const override;

	protected:
		void paintButtonBg(Painter &p, const QRect &rect, bool down, float64 howMuchOver) const override;
		void paintButtonIcon(Painter &p, const QRect &rect, HistoryMessageReplyMarkup::Button::Type type) const override;
		void paintButtonLoading(Painter &p, const QRect &rect) const override;
		int minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const override;

	};

};

inline MTPDmessage::Flags newMessageFlags(PeerData *p) {
	MTPDmessage::Flags result = 0;
	if (!p->isSelf()) {
		result |= MTPDmessage::Flag::f_out;
		if (p->isChat() || (p->isUser() && !p->asUser()->botInfo)) {
			result |= MTPDmessage::Flag::f_unread;
		}
	}
	return result;
}
inline MTPDmessage::Flags newForwardedFlags(PeerData *p, int32 from, HistoryMessage *fwd) {
	MTPDmessage::Flags result = newMessageFlags(p);
	if (from) {
		result |= MTPDmessage::Flag::f_from_id;
	}
	if (fwd->Has<HistoryMessageVia>()) {
		result |= MTPDmessage::Flag::f_via_bot_id;
	}
	if (!p->isChannel()) {
		if (HistoryMedia *media = fwd->getMedia()) {
			if (media->type() == MediaTypeVoiceFile) {
				result |= MTPDmessage::Flag::f_media_unread;
//			} else if (media->type() == MediaTypeVideo) {
//				result |= MTPDmessage::flag_media_unread;
			}
		}
	}
	return result;
}

struct HistoryServicePinned : public BaseComponent<HistoryServicePinned> {
	MsgId msgId = 0;
	HistoryItem *msg = nullptr;
	ClickHandlerPtr lnk;
};

class HistoryService : public HistoryItem, private HistoryItemInstantiated<HistoryService> {
public:

	static HistoryService *create(History *history, const MTPDmessageService &msg) {
		return _create(history, msg);
	}
	static HistoryService *create(History *history, MsgId msgId, QDateTime date, const QString &msg, MTPDmessage::Flags flags = 0, int32 from = 0) {
		return _create(history, msgId, date, msg, flags, from);
	}

	bool updateDependencyItem() override {
		return updatePinned(true);
	}
	MsgId dependencyMsgId() const override {
		if (const HistoryServicePinned *pinned = Get<HistoryServicePinned>()) {
			return pinned->msgId;
		}
		return 0;
	}
	bool notificationReady() const override {
		if (const HistoryServicePinned *pinned = Get<HistoryServicePinned>()) {
			return (pinned->msg || !pinned->msgId);
		}
		return true;
	}

	void countPositionAndSize(int32 &left, int32 &width) const;

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	bool hasPoint(int x, int y) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override {
		return _text.adjustSelection(selection, type);
	}

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override {
		if (_media) _media->clickHandlerActiveChanged(p, active);
		HistoryItem::clickHandlerActiveChanged(p, active);
	}
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override {
		if (_media) _media->clickHandlerPressedChanged(p, pressed);
		HistoryItem::clickHandlerPressedChanged(p, pressed);
	}

	void drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const override;
    QString notificationText() const override;

	bool needCheck() const override {
		return false;
	}
	bool serviceMsg() const override {
		return true;
	}
	QString selectedText(TextSelection selection) const override;
	QString inDialogsText() const override;
	QString inReplyText() const override;

	HistoryMedia *getMedia() const override;

	void setServiceText(const QString &text);

	~HistoryService();

protected:

	HistoryService(History *history, const MTPDmessageService &msg);
	HistoryService(History *history, MsgId msgId, QDateTime date, const QString &msg, MTPDmessage::Flags flags = 0, int32 from = 0);
	friend class HistoryItemInstantiated<HistoryService>;

	void initDimensions() override;
	int resizeGetHeight_(int width) override;

	void setMessageByAction(const MTPmessageAction &action);
	bool updatePinned(bool force = false);
	bool updatePinnedText(const QString *pfrom = nullptr, QString *ptext = nullptr);

};

class HistoryGroup : public HistoryService, private HistoryItemInstantiated<HistoryGroup> {
public:

	static HistoryGroup *create(History *history, const MTPDmessageGroup &group, const QDateTime &date) {
		return _create(history, group, date);
	}
	static HistoryGroup *create(History *history, HistoryItem *newItem, const QDateTime &date) {
		return _create(history, newItem, date);
	}

	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	QString selectedText(TextSelection selection) const override {
		return QString();
	}
	HistoryItemType type() const override {
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

protected:

	HistoryGroup(History *history, const MTPDmessageGroup &group, const QDateTime &date);
	HistoryGroup(History *history, HistoryItem *newItem, const QDateTime &date);
	using HistoryItemInstantiated<HistoryGroup>::_create;
	friend class HistoryItemInstantiated<HistoryGroup>;

private:
	MsgId _minId, _maxId;
	int32 _count;

	ClickHandlerPtr _lnk;

	void updateText();

};

class HistoryCollapse : public HistoryService, private HistoryItemInstantiated<HistoryCollapse> {
public:

	static HistoryCollapse *create(History *history, MsgId wasMinId, const QDateTime &date) {
		return _create(history, wasMinId, date);
	}

	void draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const override;
	HistoryTextState getState(int x, int y, HistoryStateRequest request) const override;

	QString selectedText(TextSelection selection) const override {
		return QString();
	}
	HistoryItemType type() const override {
		return HistoryItemCollapse;
	}
	MsgId wasMinId() const {
		return _wasMinId;
	}

protected:

	HistoryCollapse(History *history, MsgId wasMinId, const QDateTime &date);
	using HistoryItemInstantiated<HistoryCollapse>::_create;
	friend class HistoryItemInstantiated<HistoryCollapse>;

private:
	MsgId _wasMinId;

};

class HistoryJoined : public HistoryService, private HistoryItemInstantiated<HistoryJoined> {
public:

	static HistoryJoined *create(History *history, const QDateTime &date, UserData *from, MTPDmessage::Flags flags) {
		return _create(history, date, from, flags);
	}

	HistoryItemType type() const {
		return HistoryItemJoined;
	}

protected:

	HistoryJoined(History *history, const QDateTime &date, UserData *from, MTPDmessage::Flags flags);
	using HistoryItemInstantiated<HistoryJoined>::_create;
	friend class HistoryItemInstantiated<HistoryJoined>;

};
