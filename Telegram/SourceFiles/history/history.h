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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "data/data_types.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_common.h"
#include "ui/effects/send_action_animations.h"
#include "base/observer.h"
#include "base/timer.h"
#include "base/variant.h"
#include "base/flat_set.h"
#include "base/flags.h"

void HistoryInit();

class HistoryItem;
using HistoryItemsList = std::vector<not_null<HistoryItem*>>;

enum NewMessageType {
	NewMessageUnread,
	NewMessageLast,
	NewMessageExisting,
};

class History;
class Histories {
public:
	using Map = QHash<PeerId, History*>;
	Map map;

	Histories() : _a_typings(animation(this, &Histories::step_typings)) {
		_selfDestructTimer.setCallback([this] { checkSelfDestructItems(); });
	}

	void registerSendAction(
		not_null<History*> history,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when);
	void step_typings(TimeMs ms, bool timer);

	History *find(const PeerId &peerId);
	not_null<History*> findOrInsert(const PeerId &peerId);
	not_null<History*> findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead, int32 maxOutboxRead);

	void clear();
	void remove(const PeerId &peer);

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);

	typedef QMap<History*, TimeMs> TypingHistories; // when typing in this history started
	TypingHistories typing;
	BasicAnimation _a_typings;

	int unreadBadge() const;
	int unreadMutedCount() const {
		return _unreadMuted;
	}
	bool unreadOnlyMuted() const;
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

	void setIsPinned(History *history, bool isPinned);
	void clearPinned();
	int pinnedCount() const;
	QList<History*> getPinnedOrder() const;
	void savePinnedToServer() const;

	struct SendActionAnimationUpdate {
		History *history;
		int width;
		int height;
		bool textUpdated;
	};
	base::Observable<SendActionAnimationUpdate> &sendActionAnimationUpdated() {
		return _sendActionAnimationUpdated;
	}
	void selfDestructIn(not_null<HistoryItem*> item, TimeMs delay);

private:
	void checkSelfDestructItems();

	int _unreadFull = 0;
	int _unreadMuted = 0;
	base::Observable<SendActionAnimationUpdate> _sendActionAnimationUpdated;
	OrderedSet<History*> _pinnedDialogs;

	base::Timer _selfDestructTimer;
	std::vector<FullMsgId> _selfDestructItems;

};

class HistoryBlock;

enum HistoryMediaType {
	MediaTypePhoto,
	MediaTypeVideo,
	MediaTypeContact,
	MediaTypeCall,
	MediaTypeFile,
	MediaTypeGif,
	MediaTypeSticker,
	MediaTypeLocation,
	MediaTypeWebPage,
	MediaTypeMusicFile,
	MediaTypeVoiceFile,
	MediaTypeGame,
	MediaTypeInvoice,
	MediaTypeGrouped,

	MediaTypeCount
};

struct TextWithTags {
	struct Tag {
		int offset, length;
		QString id;
	};
	using Tags = QVector<Tag>;

	QString text;
	Tags tags;
};

inline bool operator==(const TextWithTags::Tag &a, const TextWithTags::Tag &b) {
	return (a.offset == b.offset) && (a.length == b.length) && (a.id == b.id);
}
inline bool operator!=(const TextWithTags::Tag &a, const TextWithTags::Tag &b) {
	return !(a == b);
}

inline bool operator==(const TextWithTags &a, const TextWithTags &b) {
	return (a.text == b.text) && (a.tags == b.tags);
}
inline bool operator!=(const TextWithTags &a, const TextWithTags &b) {
	return !(a == b);
}

namespace Data {
struct Draft;
} // namespace Data

class HistoryMedia;
class HistoryMessage;

enum class AddToUnreadMentionsMethod {
	New, // when new message is added to history
	Front, // when old messages slice was received
	Back, // when new messages slice was received and it is the last one, we index all media
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

	not_null<History*> migrateToOrMe() const;
	History *migrateFrom() const;

	bool isEmpty() const {
		return blocks.empty();
	}
	bool isDisplayedEmpty() const;
	bool hasOrphanMediaGroupPart() const;
	bool removeOrphanMediaGroupPart();

	void clear(bool leaveItems = false);
	void clearUpTill(MsgId availableMinId);

	void applyGroupAdminChanges(const base::flat_map<UserId, bool> &changes);

	virtual ~History();

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addToHistory(const MTPMessage &msg);
	not_null<HistoryItem*> addNewService(MsgId msgId, QDateTime date, const QString &text, MTPDmessage::Flags flags = 0, bool newMsg = true);
	not_null<HistoryItem*> addNewForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, UserId from, const QString &postAuthor, HistoryMessage *item);
	not_null<HistoryItem*> addNewDocument(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup);
	not_null<HistoryItem*> addNewPhoto(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup);
	not_null<HistoryItem*> addNewGame(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, GameData *game, const MTPReplyMarkup &markup);

	// Used only internally and for channel admin log.
	HistoryItem *createItem(const MTPMessage &msg, bool applyServiceAction, bool detachExistingItem);

	void addOlderSlice(const QVector<MTPMessage> &slice);
	void addNewerSlice(const QVector<MTPMessage> &slice);

	void newItemAdded(HistoryItem *item);

	int countUnread(MsgId upTo);
	void updateShowFrom();
	MsgId inboxRead(MsgId upTo);
	MsgId inboxRead(HistoryItem *wasRead);
	MsgId outboxRead(MsgId upTo);
	MsgId outboxRead(HistoryItem *wasRead);

	HistoryItem *lastAvailableMessage() const;

	int unreadCount() const {
		return _unreadCount;
	}
	void setUnreadCount(int newUnreadCount);
	bool mute() const {
		return _mute;
	}
	bool changeMute(bool newMute);
	void getNextShowFrom(HistoryBlock *block, int i);
	void addUnreadBar();
	void destroyUnreadBar();
	void clearNotifications();

	bool loadedAtBottom() const; // last message is in the list
	void setNotLoadedAtBottom();
	bool loadedAtTop() const; // nothing was added after loading history back
	bool isReadyFor(MsgId msgId); // has messages for showing history at msgId
	void getReadyFor(MsgId msgId);

	void setLastMessage(HistoryItem *msg);
	void fixLastMessage(bool wasAtBottom);

	bool needUpdateInChatList() const;
	void updateChatListSortPosition();
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
		return !chatListLinks(list).empty();
	}
	int posInChatList(Dialogs::Mode list) const;
	Dialogs::Row *addToChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed);
	void removeFromChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed);
	void removeChatListEntryByLetter(Dialogs::Mode list, QChar letter);
	void addChatListEntryByLetter(Dialogs::Mode list, QChar letter, Dialogs::Row *row);
	void updateChatListEntry() const;

	bool isPinnedDialog() const {
		return (_pinnedIndex > 0);
	}
	void setPinnedDialog(bool isPinned);
	void setPinnedIndex(int newPinnedIndex);
	int getPinnedIndex() const {
		return _pinnedIndex;
	}

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
	bool mySendActionUpdated(SendAction::Type type, bool doing);
	bool paintSendAction(Painter &p, int x, int y, int availableWidth, int outerWidth, style::color color, TimeMs ms);

	// Interface for Histories
	bool updateSendActionNeedsAnimating(TimeMs ms, bool force = false);
	bool updateSendActionNeedsAnimating(
		not_null<UserData*> user,
		const MTPSendMessageAction &action);

	void clearLastKeyboard();

	// optimization for userpics displayed on the left
	// if this returns false there is no need to even try to handle them
	bool canHaveFromPhotos() const;

	int getUnreadMentionsLoadedCount() const {
		return _unreadMentions.size();
	}
	MsgId getMinLoadedUnreadMention() const {
		return _unreadMentions.empty() ? 0 : _unreadMentions.front();
	}
	MsgId getMaxLoadedUnreadMention() const {
		return _unreadMentions.empty() ? 0 : _unreadMentions.back();
	}
	int getUnreadMentionsCount(int notLoadedValue = -1) const {
		return _unreadMentionsCount ? *_unreadMentionsCount : notLoadedValue;
	}
	bool hasUnreadMentions() const {
		return (getUnreadMentionsCount() > 0);
	}
	void setUnreadMentionsCount(int count);
	bool addToUnreadMentions(MsgId msgId, AddToUnreadMentionsMethod method);
	void eraseFromUnreadMentions(MsgId msgId);
	void addUnreadMentionsSlice(const MTPmessages_Messages &result);

	using Blocks = std::deque<HistoryBlock*>;
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

	Data::Draft *localDraft() {
		return _localDraft.get();
	}
	Data::Draft *cloudDraft() {
		return _cloudDraft.get();
	}
	Data::Draft *editDraft() {
		return _editDraft.get();
	}
	void setLocalDraft(std::unique_ptr<Data::Draft> &&draft);
	void takeLocalDraft(History *from);
	void createLocalDraftFromCloud();
	void setCloudDraft(std::unique_ptr<Data::Draft> &&draft);
	Data::Draft *createCloudDraft(Data::Draft *fromDraft);
	void setEditDraft(std::unique_ptr<Data::Draft> &&draft);
	void clearLocalDraft();
	void clearCloudDraft();
	void clearEditDraft();
	void draftSavedToCloud();
	Data::Draft *draft() {
		return _editDraft ? editDraft() : localDraft();
	}

	const MessageIdsList &forwardDraft() const {
		return _forwardDraft;
	}
	HistoryItemsList validateForwardDraft();
	void setForwardDraft(MessageIdsList &&items);
	void recountGroupingAround(not_null<HistoryItem*> item);

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
	mutable Text lastItemTextCache;

	void changeMsgId(MsgId oldId, MsgId newId);

	Text cloudDraftTextCache;

protected:
	void clearOnDestroy();
	HistoryItem *addNewToLastBlock(const MTPMessage &msg, NewMessageType type);

	friend class HistoryBlock;

	// this method just removes a block from the blocks list
	// when the last item from this block was detached and
	// calls the required previousItemChanged()
	void removeBlock(not_null<HistoryBlock*> block);

	void clearBlocks(bool leaveItems);

	not_null<HistoryItem*> createItemForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, UserId from, const QString &postAuthor, HistoryMessage *msg);
	not_null<HistoryItem*> createItemDocument(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup);
	not_null<HistoryItem*> createItemPhoto(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup);
	not_null<HistoryItem*> createItemGame(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, GameData *game, const MTPReplyMarkup &markup);

	not_null<HistoryItem*> addNewItem(not_null<HistoryItem*> adding, bool newMsg);
	not_null<HistoryItem*> addNewInTheMiddle(not_null<HistoryItem*> newItem, int32 blockIndex, int32 itemIndex);

	// All this methods add a new item to the first or last block
	// depending on if we are in isBuildingFronBlock() state.
	// The last block is created on the go if it is needed.

	// Adds the item to the back or front block, depending on
	// isBuildingFrontBlock(), creating the block if necessary.
	void addItemToBlock(not_null<HistoryItem*> item);

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

	// Add all items to the unread mentions if we were not loaded at bottom and now are.
	void checkAddAllToUnreadMentions();

	template <int kSharedMediaTypeCount>
	void addToSharedMedia(std::vector<MsgId> (&medias)[kSharedMediaTypeCount], bool force);
	void addBlockToSharedMedia(HistoryBlock *block);

	void clearSendAction(not_null<UserData*> from);

	HistoryItem *findPreviousItem(not_null<HistoryItem*> item) const;
	HistoryItem *findNextItem(not_null<HistoryItem*> item) const;
	not_null<HistoryItem*> findGroupFirst(
		not_null<HistoryItem*> item) const;
	not_null<HistoryItem*> findGroupLast(
		not_null<HistoryItem*> item) const;
	auto recountGroupingFromTill(not_null<HistoryItem*> item)
		-> std::pair<not_null<HistoryItem*>, not_null<HistoryItem*>>;
	void recountGrouping(
		not_null<HistoryItem*> from,
		not_null<HistoryItem*> till);

	enum class Flag {
		f_has_pending_resized_items = (1 << 0),
		f_pending_resize            = (1 << 1),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; };

	Flags _flags = 0;
	bool _mute = false;
	int _unreadCount = 0;

	base::optional<int> _unreadMentionsCount;
	base::flat_set<MsgId> _unreadMentions;

	Dialogs::RowsByLetter _chatListLinks[2];
	Dialogs::RowsByLetter &chatListLinks(Dialogs::Mode list) {
		return _chatListLinks[static_cast<int>(list)];
	}
	const Dialogs::RowsByLetter &chatListLinks(Dialogs::Mode list) const {
		return _chatListLinks[static_cast<int>(list)];
	}
	Dialogs::Row *mainChatListLink(Dialogs::Mode list) const {
		auto it = chatListLinks(list).find(0);
		Assert(it != chatListLinks(list).cend());
		return it->second;
	}
	uint64 _sortKeyInChatList = 0; // like ((unixtime) << 32) | (incremented counter)

	// A pointer to the block that is currently being built.
	// We hold this pointer so we can destroy it while building
	// and then create a new one if it is necessary.
	struct BuildingBlock {
		int expectedItemsCount = 0; // optimization for block->items.reserve() call
		HistoryBlock *block = nullptr;
	};
	std::unique_ptr<BuildingBlock> _buildingFrontBlock;

	// Creates if necessary a new block for adding item.
	// Depending on isBuildingFrontBlock() gets front or back block.
	HistoryBlock *prepareBlockForAddingItem();

	std::unique_ptr<Data::Draft> _localDraft, _cloudDraft;
	std::unique_ptr<Data::Draft> _editDraft;
	MessageIdsList _forwardDraft;

	using TypingUsers = QMap<UserData*, TimeMs>;
	TypingUsers _typing;
	using SendActionUsers = QMap<UserData*, SendAction>;
	SendActionUsers _sendActions;
	QString _sendActionString;
	Text _sendActionText;
	Ui::SendActionAnimation _sendActionAnimation;
	QMap<SendAction::Type, TimeMs> _mySendActions;

	int _pinnedIndex = 0; // > 0 for pinned dialogs

 };

class HistoryJoined;
class ChannelHistory : public History {
public:
	using History::History;

	void messageDetached(HistoryItem *msg);

	void getRangeDifference();
	void getRangeDifferenceNext(int32 pts);

	HistoryJoined *insertJoinedMessage(bool unread);
	void checkJoinedMessage(bool createUnread = false);
	const QDateTime &maxReadMessageDate();

	~ChannelHistory();

private:
	friend class History;
	HistoryItem* addNewChannelMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addNewToBlocks(const MTPMessage &msg, NewMessageType type);

	void checkMaxReadMessageDate();

	void cleared(bool leaveItems);

	QDateTime _maxReadMessageDate;

	HistoryJoined *_joinedMessage = nullptr;

	MsgId _rangeDifferenceFromId, _rangeDifferenceToId;
	int32 _rangeDifferencePts;
	mtpRequestId _rangeDifferenceRequestId;

};

class HistoryBlock {
public:
	HistoryBlock(not_null<History*> history) : _history(history) {
	}

	HistoryBlock(const HistoryBlock &) = delete;
	HistoryBlock &operator=(const HistoryBlock &) = delete;

	std::vector<HistoryItem*> items;

	void clear(bool leaveItems = false);
	~HistoryBlock() {
		clear();
	}
	void removeItem(not_null<HistoryItem*> item);

	int resizeGetHeight(int newWidth, bool resizeAllItems);
	int y() const {
		return _y;
	}
	void setY(int y) {
		_y = y;
	}
	int height() const {
		return _height;
	}
	not_null<History*> history() const {
		return _history;
	}

	HistoryBlock *previousBlock() const {
		Expects(_indexInHistory >= 0);

		return (_indexInHistory > 0) ? _history->blocks.at(_indexInHistory - 1) : nullptr;
	}
	HistoryBlock *nextBlock() const {
		Expects(_indexInHistory >= 0);

		return (_indexInHistory + 1 < _history->blocks.size()) ? _history->blocks.at(_indexInHistory + 1) : nullptr;
	}
	void setIndexInHistory(int index) {
		_indexInHistory = index;
	}
	int indexInHistory() const {
		Expects(_indexInHistory >= 0);
		Expects(_indexInHistory < _history->blocks.size());
		Expects(_history->blocks[_indexInHistory] == this);

		return _indexInHistory;
	}

protected:
	const not_null<History*> _history;

	int _y = 0;
	int _height = 0;
	int _indexInHistory = -1;

};
