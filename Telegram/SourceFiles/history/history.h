/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_entry.h"
#include "ui/effects/send_action_animations.h"
#include "base/observer.h"
#include "base/timer.h"
#include "base/variant.h"
#include "base/flat_set.h"
#include "base/flags.h"

class History;
class HistoryBlock;
class HistoryItem;
class HistoryMessage;
class HistoryService;
class HistoryMedia;
class AuthSession;

namespace Data {
struct Draft;
} // namespace Data

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace AdminLog {
class LocalIdManager;
} // namespace AdminLog

enum NewMessageType : char {
	NewMessageUnread,
	NewMessageLast,
	NewMessageExisting,
};

class Histories {
public:
	Histories();

	void registerSendAction(
		not_null<History*> history,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when);
	void step_typings(TimeMs ms, bool timer);

	History *find(PeerId peerId) const;
	not_null<History*> findOrInsert(PeerId peerId);

	void clear();
	void remove(const PeerId &peer);

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);

	// When typing in this history started.
	typedef QMap<History*, TimeMs> TypingHistories;
	TypingHistories typing;
	BasicAnimation _a_typings;

	int unreadBadge() const;
	bool unreadBadgeMuted() const;
	int unreadBadgeIgnoreOne(History *history) const;
	bool unreadBadgeMutedIgnoreOne(History *history) const;
	int unreadOnlyMutedBadge() const;

	void unreadIncrement(int count, bool muted);
	void unreadMuteChanged(int count, bool muted);
	void unreadEntriesChanged(
		int withUnreadDelta,
		int mutedWithUnreadDelta);

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
	int computeUnreadBadge(
		int full,
		int muted,
		int entriesFull,
		int entriesMuted) const;
	bool computeUnreadBadgeMuted(
		int full,
		int muted,
		int entriesFull,
		int entriesMuted) const;

	std::unordered_map<PeerId, std::unique_ptr<History>> _map;

	int _unreadFull = 0;
	int _unreadMuted = 0;
	int _unreadEntriesFull = 0;
	int _unreadEntriesMuted = 0;
	base::Observable<SendActionAnimationUpdate> _sendActionAnimationUpdated;

	base::Timer _selfDestructTimer;
	std::vector<FullMsgId> _selfDestructItems;

};

enum class UnreadMentionType {
	New, // when new message is added to history
	Existing, // when some messages slice was received
};

class History : public Dialogs::Entry {
public:
	using Element = HistoryView::Element;

	History(const PeerId &peerId);
	History(const History &) = delete;
	History &operator=(const History &) = delete;

	ChannelId channelId() const;
	bool isChannel() const;
	bool isMegagroup() const;
	not_null<History*> migrateToOrMe() const;
	History *migrateFrom() const;
	MsgRange rangeForDifferenceRequest() const;
	HistoryService *insertJoinedMessage(bool unread);
	void checkJoinedMessage(bool createUnread = false);
	void removeJoinedMessage();

	bool isEmpty() const;
	bool isDisplayedEmpty() const;
	bool hasOrphanMediaGroupPart() const;
	bool removeOrphanMediaGroupPart();
	QVector<MsgId> collectMessagesFromUserToDelete(
		not_null<UserData*> user) const;

	void clear();
	void markFullyLoaded();
	void unloadBlocks();
	void clearUpTill(MsgId availableMinId);

	void applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes);

	HistoryItem *addNewMessage(const MTPMessage &msg, NewMessageType type);
	HistoryItem *addToHistory(const MTPMessage &msg);
	not_null<HistoryItem*> addNewService(
		MsgId msgId,
		TimeId date,
		const QString &text,
		MTPDmessage::Flags flags = 0,
		bool newMsg = true);
	not_null<HistoryItem*> addNewForwarded(
		MsgId id,
		MTPDmessage::Flags flags,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> original);
	not_null<HistoryItem*> addNewDocument(
		MsgId id,
		MTPDmessage::Flags flags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup);
	not_null<HistoryItem*> addNewPhoto(
		MsgId id,
		MTPDmessage::Flags flags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup);
	not_null<HistoryItem*> addNewGame(
		MsgId id,
		MTPDmessage::Flags flags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup);

	// Used only internally and for channel admin log.
	HistoryItem *createItem(
		const MTPMessage &message,
		bool detachExistingItem);
	std::vector<not_null<HistoryItem*>> createItems(
		const QVector<MTPMessage> &data);

	void addOlderSlice(const QVector<MTPMessage> &slice);
	void addNewerSlice(const QVector<MTPMessage> &slice);

	void newItemAdded(not_null<HistoryItem*> item);

	int countUnread(MsgId upTo);
	MsgId readInbox();
	void inboxRead(MsgId upTo);
	void inboxRead(not_null<const HistoryItem*> wasRead);
	void outboxRead(MsgId upTo);
	void outboxRead(not_null<const HistoryItem*> wasRead);
	bool isServerSideUnread(not_null<const HistoryItem*> item) const;
	MsgId loadAroundId() const;

	int unreadCount() const;
	bool unreadCountKnown() const;
	void setUnreadCount(int newUnreadCount);
	void changeUnreadCount(int delta);
	void setUnreadMark(bool unread);
	bool unreadMark() const;
	int historiesUnreadCount() const; // unreadCount || unreadMark ? 1 : 0.
	bool mute() const;
	bool changeMute(bool newMute);
	void addUnreadBar();
	void destroyUnreadBar();
	bool hasNotFreezedUnreadBar() const;
	Element *unreadBar() const;
	void calculateFirstUnreadMessage();
	void unsetFirstUnreadMessage();
	Element *firstUnreadMessage() const;
	void clearNotifications();

	bool loadedAtBottom() const; // last message is in the list
	void setNotLoadedAtBottom();
	bool loadedAtTop() const; // nothing was added after loading history back
	bool isReadyFor(MsgId msgId); // has messages for showing history at msgId
	void getReadyFor(MsgId msgId);

	HistoryItem *lastMessage() const;
	bool lastMessageKnown() const;
	void unknownMessageDeleted(MsgId messageId);
	void applyDialogTopMessage(MsgId topMessageId);
	void applyDialog(const MTPDdialog &data);
	void applyDialogFields(
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead);

	MsgId minMsgId() const;
	MsgId maxMsgId() const;
	MsgId msgIdForRead() const;
	HistoryItem *lastSentMessage() const;

	void resizeToWidth(int newWidth);
	int height() const;

	void itemRemoved(not_null<HistoryItem*> item);
	void itemVanished(not_null<HistoryItem*> item);

	HistoryItem *currentNotification();
	bool hasNotification() const;
	void skipNotification();
	void popNotification(HistoryItem *item);

	bool hasPendingResizedItems() const;
	void setHasPendingResizedItems();

	bool mySendActionUpdated(SendAction::Type type, bool doing);
	bool paintSendAction(Painter &p, int x, int y, int availableWidth, int outerWidth, style::color color, TimeMs ms);

	// Interface for Histories
	bool updateSendActionNeedsAnimating(TimeMs ms, bool force = false);
	bool updateSendActionNeedsAnimating(
		not_null<UserData*> user,
		const MTPSendMessageAction &action);

	void clearLastKeyboard();

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
	bool addToUnreadMentions(MsgId msgId, UnreadMentionType type);
	void eraseFromUnreadMentions(MsgId msgId);
	void addUnreadMentionsSlice(const MTPmessages_Messages &result);

	Data::Draft *localDraft() const {
		return _localDraft.get();
	}
	Data::Draft *cloudDraft() const {
		return _cloudDraft.get();
	}
	Data::Draft *editDraft() const {
		return _editDraft.get();
	}
	void setLocalDraft(std::unique_ptr<Data::Draft> &&draft);
	void takeLocalDraft(History *from);
	void createLocalDraftFromCloud();
	void setCloudDraft(std::unique_ptr<Data::Draft> &&draft);
	Data::Draft *createCloudDraft(const Data::Draft *fromDraft);
	bool skipCloudDraft(const QString &text, TimeId date) const;
	void setSentDraftText(const QString &text);
	void clearSentDraftText(const QString &text);
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

	History *migrateSibling() const;
	bool useProxyPromotion() const override;
	void updateChatListExistence() override;
	bool shouldBeInChatList() const override;
	bool toImportant() const override;
	int chatListUnreadCount() const override;
	bool chatListUnreadMark() const override;
	bool chatListMutedBadge() const override;
	HistoryItem *chatsListItem() const override;
	const QString &chatsListName() const override;
	const base::flat_set<QString> &chatsListNameWords() const override;
	const base::flat_set<QChar> &chatsListFirstLetters() const override;
	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const override;

	void forgetScrollState() {
		scrollTopItem = nullptr;
	}

	// find the correct scrollTopItem and scrollTopOffset using given top
	// of the displayed window relative to the history start coordinate
	void countScrollState(int top);

	std::shared_ptr<AdminLog::LocalIdManager> adminLogIdManager();

	virtual ~History();

	// Still public data.
	std::deque<std::unique_ptr<HistoryBlock>> blocks;

	not_null<PeerData*> peer;

	typedef QList<HistoryItem*> NotifyQueue;
	NotifyQueue notifies;

	// we save the last showAtMsgId to restore the state when switching
	// between different conversation histories
	MsgId showAtMsgId = ShowAtUnreadMsgId;

	// we save a pointer of the history item at the top of the displayed window
	// together with an offset from the window top to the top of this message
	// resulting scrollTop = top(scrollTopItem) + scrollTopOffset
	Element *scrollTopItem = nullptr;
	int scrollTopOffset = 0;

	bool lastKeyboardInited = false;
	bool lastKeyboardUsed = false;
	MsgId lastKeyboardId = 0;
	MsgId lastKeyboardHiddenId = 0;
	PeerId lastKeyboardFrom = 0;

	mtpRequestId sendRequestId = 0;

	Text cloudDraftTextCache;

private:
	friend class HistoryBlock;

	enum class Flag {
		f_has_pending_resized_items = (1 << 0),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) {
		return true;
	};

	// when this item is destroyed scrollTopItem just points to the next one
	// and scrollTopOffset remains the same
	// if we are at the bottom of the window scrollTopItem == nullptr and
	// scrollTopOffset is undefined
	void getNextScrollTopItem(HistoryBlock *block, int32 i);

	// helper method for countScrollState(int top)
	void countScrollTopItem(int top);

	HistoryItem *addNewToLastBlock(const MTPMessage &msg, NewMessageType type);

	// this method just removes a block from the blocks list
	// when the last item from this block was detached and
	// calls the required previousItemChanged()
	void removeBlock(not_null<HistoryBlock*> block);

	void clearBlocks(bool leaveItems);

	not_null<HistoryItem*> addNewItem(
		not_null<HistoryItem*> item,
		bool unread);
	not_null<HistoryItem*> addNewInTheMiddle(
		not_null<HistoryItem*> item,
		int blockIndex,
		int itemIndex);

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
	void finishBuildingFrontBlock();
	bool isBuildingFrontBlock() const {
		return _buildingFrontBlock != nullptr;
	}

	void mainViewRemoved(
		not_null<HistoryBlock*> block,
		not_null<Element*> view);
	void removeNotification(not_null<HistoryItem*> item);

	TimeId adjustChatListTimeId() const override;
	void changedInChatListHook(Dialogs::Mode list, bool added) override;
	void changedChatListPinHook() override;

	void setInboxReadTill(MsgId upTo);
	void setOutboxReadTill(MsgId upTo);

	void applyMessageChanges(
		not_null<HistoryItem*> item,
		const MTPMessage &original);
	void applyServiceChanges(
		not_null<HistoryItem*> item,
		const MTPDmessageService &data);

	// After adding a new history slice check lastMessage / loadedAtBottom.
	void checkLastMessage();
	void setLastMessage(HistoryItem *item);

	// Add all items to the unread mentions if we were not loaded at bottom and now are.
	void checkAddAllToUnreadMentions();

	void addToSharedMedia(const std::vector<not_null<HistoryItem*>> &items);
	void addEdgesToSharedMedia();

	void addItemsToLists(const std::vector<not_null<HistoryItem*>> &items);
	void clearSendAction(not_null<UserData*> from);
	bool clearUnreadOnClientSide() const;
	bool skipUnreadUpdate() const;
	bool skipUnreadUpdateForClientSideUnread() const;

	HistoryItem *lastAvailableMessage() const;
	void getNextFirstUnreadMessage();

	// Creates if necessary a new block for adding item.
	// Depending on isBuildingFrontBlock() gets front or back block.
	HistoryBlock *prepareBlockForAddingItem();

	void viewReplaced(not_null<const Element*> was, Element *now);

	Flags _flags = 0;
	bool _mute = false;
	int _width = 0;
	int _height = 0;
	Element *_unreadBarView = nullptr;
	Element *_firstUnreadView = nullptr;
	HistoryService *_joinedMessage = nullptr;
	bool _loadedAtTop = false;
	bool _loadedAtBottom = true;

	std::optional<MsgId> _inboxReadBefore;
	std::optional<MsgId> _outboxReadBefore;
	std::optional<int> _unreadCount;
	std::optional<int> _unreadMentionsCount;
	base::flat_set<MsgId> _unreadMentions;
	std::optional<HistoryItem*> _lastMessage;
	bool _unreadMark = false;

	// A pointer to the block that is currently being built.
	// We hold this pointer so we can destroy it while building
	// and then create a new one if it is necessary.
	struct BuildingBlock {
		int expectedItemsCount = 0; // optimization for block->items.reserve() call
		HistoryBlock *block = nullptr;
	};
	std::unique_ptr<BuildingBlock> _buildingFrontBlock;

	std::unique_ptr<Data::Draft> _localDraft, _cloudDraft;
	std::unique_ptr<Data::Draft> _editDraft;
	std::optional<QString> _lastSentDraftText;
	TimeId _lastSentDraftTime = 0;
	MessageIdsList _forwardDraft;

	using TypingUsers = QMap<UserData*, TimeMs>;
	TypingUsers _typing;
	using SendActionUsers = QMap<UserData*, SendAction>;
	SendActionUsers _sendActions;
	QString _sendActionString;
	Text _sendActionText;
	Ui::SendActionAnimation _sendActionAnimation;
	QMap<SendAction::Type, TimeMs> _mySendActions;

	std::weak_ptr<AdminLog::LocalIdManager> _adminLogIdManager;

 };

class HistoryBlock {
public:
	using Element = HistoryView::Element;

	HistoryBlock(not_null<History*> history);
	HistoryBlock(const HistoryBlock &) = delete;
	HistoryBlock &operator=(const HistoryBlock &) = delete;
	~HistoryBlock();

	std::vector<std::unique_ptr<Element>> messages;

	void remove(not_null<Element*> view);
	void refreshView(not_null<Element*> view);

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

		return (_indexInHistory > 0)
			? _history->blocks[_indexInHistory - 1].get()
			: nullptr;
	}
	HistoryBlock *nextBlock() const {
		Expects(_indexInHistory >= 0);

		return (_indexInHistory + 1 < _history->blocks.size())
			? _history->blocks[_indexInHistory + 1].get()
			: nullptr;
	}
	void setIndexInHistory(int index) {
		_indexInHistory = index;
	}
	int indexInHistory() const {
		Expects(_indexInHistory >= 0);
		Expects(_indexInHistory < _history->blocks.size());
		Expects(_history->blocks[_indexInHistory].get() == this);

		return _indexInHistory;
	}

protected:
	const not_null<History*> _history;

	int _y = 0;
	int _height = 0;
	int _indexInHistory = -1;

};
