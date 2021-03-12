/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "data/data_peer.h"
#include "data/data_drafts.h"
#include "dialogs/dialogs_entry.h"
#include "history/view/history_view_send_action.h"
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

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct Draft;
class Session;
class Folder;
class ChatFilter;
} // namespace Data

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace HistoryView {
class Element;
} // namespace HistoryView

enum class NewMessageType {
	Unread,
	Last,
	Existing,
};

enum class UnreadMentionType {
	New, // when new message is added to history
	Existing, // when some messages slice was received
};

class History final : public Dialogs::Entry {
public:
	using Element = HistoryView::Element;

	History(not_null<Data::Session*> owner, PeerId peerId);
	History(const History &) = delete;
	History &operator=(const History &) = delete;
	~History();

	ChannelId channelId() const;
	bool isChannel() const;
	bool isMegagroup() const;
	not_null<History*> migrateToOrMe() const;
	History *migrateFrom() const;
	MsgRange rangeForDifferenceRequest() const;
	void checkLocalMessages();
	void removeJoinedMessage();


	bool isEmpty() const;
	bool isDisplayedEmpty() const;
	Element *findFirstNonEmpty() const;
	Element *findFirstDisplayed() const;
	Element *findLastNonEmpty() const;
	Element *findLastDisplayed() const;
	bool hasOrphanMediaGroupPart() const;
	bool removeOrphanMediaGroupPart();
	QVector<MsgId> collectMessagesFromUserToDelete(
		not_null<UserData*> user) const;

	enum class ClearType {
		Unload,
		DeleteChat,
		ClearHistory,
	};
	void clear(ClearType type);
	void clearUpTill(MsgId availableMinId);

	void applyGroupAdminChanges(const base::flat_set<UserId> &changes);

	template <typename ...Args>
	not_null<HistoryMessage*> makeMessage(Args &&...args) {
		return static_cast<HistoryMessage*>(
			insertItem(
				std::make_unique<HistoryMessage>(
					this,
					std::forward<Args>(args)...)).get());
	}

	template <typename ...Args>
	not_null<HistoryService*> makeServiceMessage(Args &&...args) {
		return static_cast<HistoryService*>(
			insertItem(
				std::make_unique<HistoryService>(
					this,
					std::forward<Args>(args)...)).get());
	}
	void destroyMessage(not_null<HistoryItem*> item);

	void unpinAllMessages();

	HistoryItem *addNewMessage(
		const MTPMessage &msg,
		MTPDmessage_ClientFlags clientFlags,
		NewMessageType type);
	HistoryItem *addToHistory(
		const MTPMessage &msg,
		MTPDmessage_ClientFlags clientFlags);
	not_null<HistoryItem*> addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> forwardOriginal);
	not_null<HistoryItem*> addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup);
	not_null<HistoryItem*> addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup);
	not_null<HistoryItem*> addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup);

	// Used only internally and for channel admin log.
	HistoryItem *createItem(
		const MTPMessage &message,
		MTPDmessage_ClientFlags clientFlags,
		bool detachExistingItem);
	std::vector<not_null<HistoryItem*>> createItems(
		const QVector<MTPMessage> &data);

	void addOlderSlice(const QVector<MTPMessage> &slice);
	void addNewerSlice(const QVector<MTPMessage> &slice);

	void newItemAdded(not_null<HistoryItem*> item);

	void registerLocalMessage(not_null<HistoryItem*> item);
	void unregisterLocalMessage(not_null<HistoryItem*> item);
	[[nodiscard]] auto localMessages()
		-> const base::flat_set<not_null<HistoryItem*>> &;
	[[nodiscard]] HistoryItem *latestSendingMessage() const;

	[[nodiscard]] bool readInboxTillNeedsRequest(MsgId tillId);
	void applyInboxReadUpdate(
		FolderId folderId,
		MsgId upTo,
		int stillUnread,
		int32 channelPts = 0);
	void inboxRead(MsgId upTo, std::optional<int> stillUnread = {});
	void inboxRead(not_null<const HistoryItem*> wasRead);
	void outboxRead(MsgId upTo);
	void outboxRead(not_null<const HistoryItem*> wasRead);
	[[nodiscard]] bool isServerSideUnread(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] MsgId loadAroundId() const;
	[[nodiscard]] MsgId inboxReadTillId() const;
	[[nodiscard]] MsgId outboxReadTillId() const;

	[[nodiscard]] bool trackUnreadMessages() const;
	[[nodiscard]] int unreadCount() const;
	[[nodiscard]] bool unreadCountKnown() const;

	// Some old unread count is known, but we read history till some place.
	[[nodiscard]] bool unreadCountRefreshNeeded(MsgId readTillId) const;

	void setUnreadCount(int newUnreadCount);
	void setUnreadMark(bool unread);
	[[nodiscard]] bool unreadMark() const;
	void setFakeUnreadWhileOpened(bool enabled);
	[[nodiscard]] bool fakeUnreadWhileOpened() const;
	[[nodiscard]] int unreadCountForBadge() const; // unreadCount || unreadMark ? 1 : 0.
	[[nodiscard]] bool mute() const;
	bool changeMute(bool newMute);
	void addUnreadBar();
	void destroyUnreadBar();
	[[nodiscard]] Element *unreadBar() const;
	void calculateFirstUnreadMessage();
	void unsetFirstUnreadMessage();
	[[nodiscard]] Element *firstUnreadMessage() const;
	void clearNotifications();
	void clearIncomingNotifications();

	[[nodiscard]] bool loadedAtBottom() const; // last message is in the list
	void setNotLoadedAtBottom();
	[[nodiscard]] bool loadedAtTop() const; // nothing was added after loading history back
	[[nodiscard]] bool isReadyFor(MsgId msgId); // has messages for showing history at msgId
	void getReadyFor(MsgId msgId);

	[[nodiscard]] HistoryItem *lastMessage() const;
	[[nodiscard]] HistoryItem *lastServerMessage() const;
	[[nodiscard]] bool lastMessageKnown() const;
	[[nodiscard]] bool lastServerMessageKnown() const;
	void unknownMessageDeleted(MsgId messageId);
	void applyDialogTopMessage(MsgId topMessageId);
	void applyDialog(Data::Folder *requestFolder, const MTPDdialog &data);
	void applyPinnedUpdate(const MTPDupdateDialogPinned &data);
	void applyDialogFields(
		Data::Folder *folder,
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead);
	void dialogEntryApplied();

	void cacheTopPromotion(
		bool promoted,
		const QString &type,
		const QString &message);
	[[nodiscard]] QStringRef topPromotionType() const;
	[[nodiscard]] QString topPromotionMessage() const;
	[[nodiscard]] bool topPromotionAboutShown() const;
	void markTopPromotionAboutShown();

	MsgId minMsgId() const;
	MsgId maxMsgId() const;
	MsgId msgIdForRead() const;
	HistoryItem *lastEditableMessage() const;

	void resizeToWidth(int newWidth);
	void forceFullResize();
	int height() const;

	void itemRemoved(not_null<HistoryItem*> item);
	void itemVanished(not_null<HistoryItem*> item);

	HistoryItem *currentNotification();
	bool hasNotification() const;
	void skipNotification();
	void popNotification(HistoryItem *item);

	bool hasPendingResizedItems() const;
	void setHasPendingResizedItems();

	[[nodiscard]] auto sendActionPainter()
	-> not_null<HistoryView::SendActionPainter*> {
		return &_sendActionPainter;
	}

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

	Data::Draft *draft(Data::DraftKey key) const;
	void setDraft(Data::DraftKey key, std::unique_ptr<Data::Draft> &&draft);
	void clearDraft(Data::DraftKey key);

	[[nodiscard]] const Data::HistoryDrafts &draftsMap() const;
	void setDraftsMap(Data::HistoryDrafts &&map);

	Data::Draft *localDraft() const {
		return draft(Data::DraftKey::Local());
	}
	Data::Draft *localEditDraft() const {
		return draft(Data::DraftKey::LocalEdit());
	}
	Data::Draft *cloudDraft() const {
		return draft(Data::DraftKey::Cloud());
	}
	void setLocalDraft(std::unique_ptr<Data::Draft> &&draft) {
		setDraft(Data::DraftKey::Local(), std::move(draft));
	}
	void setLocalEditDraft(std::unique_ptr<Data::Draft> &&draft) {
		setDraft(Data::DraftKey::LocalEdit(), std::move(draft));
	}
	void setCloudDraft(std::unique_ptr<Data::Draft> &&draft) {
		setDraft(Data::DraftKey::Cloud(), std::move(draft));
	}
	void clearLocalDraft() {
		clearDraft(Data::DraftKey::Local());
	}
	void clearCloudDraft() {
		clearDraft(Data::DraftKey::Cloud());
	}
	void clearLocalEditDraft() {
		clearDraft(Data::DraftKey::LocalEdit());
	}
	void clearDrafts();
	Data::Draft *createCloudDraft(const Data::Draft *fromDraft);
	bool skipCloudDraftUpdate(TimeId date) const;
	void startSavingCloudDraft();
	void finishSavingCloudDraft(TimeId savedAt);
	void takeLocalDraft(not_null<History*> from);
	void applyCloudDraft();
	void draftSavedToCloud();

	const MessageIdsList &forwardDraft() const {
		return _forwardDraft;
	}
	HistoryItemsList validateForwardDraft();
	void setForwardDraft(MessageIdsList &&items);

	History *migrateSibling() const;
	[[nodiscard]] bool useTopPromotion() const;
	int fixedOnTopIndex() const override;
	void updateChatListExistence() override;
	bool shouldBeInChatList() const override;
	int chatListUnreadCount() const override;
	bool chatListUnreadMark() const override;
	bool chatListMutedBadge() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	void requestChatListMessage() override;
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;
	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const override;

	void refreshChatListNameSortKey();

	void setFakeChatListMessageFrom(const MTPmessages_Messages &data);
	void checkChatListMessageRemoved(not_null<HistoryItem*> item);

	void forgetScrollState() {
		scrollTopItem = nullptr;
	}

	// find the correct scrollTopItem and scrollTopOffset using given top
	// of the displayed window relative to the history start coordinate
	void countScrollState(int top);

	[[nodiscard]] std::pair<Element*, int> findItemAndOffset(int top) const;

	MsgId nextNonHistoryEntryId();

	bool folderKnown() const override;
	Data::Folder *folder() const override;
	void setFolder(
		not_null<Data::Folder*> folder,
		HistoryItem *folderDialogItem = nullptr);
	void clearFolder();

	// Interface for Data::Histories.
	void setInboxReadTill(MsgId upTo);
	std::optional<int> countStillUnreadLocal(MsgId readTillId) const;

	// Still public data.
	std::deque<std::unique_ptr<HistoryBlock>> blocks;

	not_null<PeerData*> peer;

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

	Ui::Text::String cloudDraftTextCache;

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
	[[nodiscard]] Element *findScrollTopItem(int top) const;

	// this method just removes a block from the blocks list
	// when the last item from this block was detached and
	// calls the required previousItemChanged()
	void removeBlock(not_null<HistoryBlock*> block);
	void clearSharedMedia();

	not_null<HistoryItem*> insertItem(std::unique_ptr<HistoryItem> item);
	not_null<HistoryItem*> addNewItem(
		not_null<HistoryItem*> item,
		bool unread);
	not_null<HistoryItem*> addNewToBack(
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

	void checkForLoadedAtTop(not_null<HistoryItem*> added);
	void mainViewRemoved(
		not_null<HistoryBlock*> block,
		not_null<Element*> view);
	void removeNotification(not_null<HistoryItem*> item);

	TimeId adjustedChatListTimeId() const override;
	void changedChatListPinHook() override;

	void setOutboxReadTill(MsgId upTo);
	void readClientSideMessages();

	void applyMessageChanges(
		not_null<HistoryItem*> item,
		const MTPMessage &original);
	void applyServiceChanges(
		not_null<HistoryItem*> item,
		const MTPDmessageService &data);

	// After adding a new history slice check lastMessage / loadedAtBottom.
	void checkLastMessage();
	void setLastMessage(HistoryItem *item);
	void setLastServerMessage(HistoryItem *item);

	void refreshChatListMessage();
	void setChatListMessage(HistoryItem *item);
	std::optional<HistoryItem*> computeChatListMessageFromLast() const;
	void setChatListMessageFromLast();
	void setChatListMessageUnknown();
	void setFakeChatListMessage();

	// Add all items to the unread mentions if we were not loaded at bottom and now are.
	void checkAddAllToUnreadMentions();

	void addToSharedMedia(const std::vector<not_null<HistoryItem*>> &items);
	void addEdgesToSharedMedia();

	void addItemsToLists(const std::vector<not_null<HistoryItem*>> &items);
	bool clearUnreadOnClientSide() const;
	bool skipUnreadUpdate() const;

	HistoryItem *lastAvailableMessage() const;
	void getNextFirstUnreadMessage();
	bool nonEmptyCountMoreThan(int count) const;

	// Creates if necessary a new block for adding item.
	// Depending on isBuildingFrontBlock() gets front or back block.
	HistoryBlock *prepareBlockForAddingItem();

	void viewReplaced(not_null<const Element*> was, Element *now);

	void createLocalDraftFromCloud();

	HistoryService *insertJoinedMessage();
	void insertLocalMessage(not_null<HistoryItem*> item);

	void setFolderPointer(Data::Folder *folder);

	Flags _flags = 0;
	bool _mute = false;
	int _width = 0;
	int _height = 0;
	Element *_unreadBarView = nullptr;
	Element *_firstUnreadView = nullptr;
	HistoryService *_joinedMessage = nullptr;
	bool _loadedAtTop = false;
	bool _loadedAtBottom = true;

	std::optional<Data::Folder*> _folder;

	std::optional<MsgId> _inboxReadBefore;
	std::optional<MsgId> _outboxReadBefore;
	std::optional<int> _unreadCount;
	std::optional<int> _unreadMentionsCount;
	base::flat_set<MsgId> _unreadMentions;
	std::optional<HistoryItem*> _lastMessage;
	std::optional<HistoryItem*> _lastServerMessage;
	base::flat_set<not_null<HistoryItem*>> _localMessages;
	std::unordered_set<std::unique_ptr<HistoryItem>> _messages;

	// This almost always is equal to _lastMessage. The only difference is
	// for a group that migrated to a supergroup. Then _lastMessage can
	// be a migrate message, but _chatListMessage should be the one before.
	std::optional<HistoryItem*> _chatListMessage;

	QString _chatListNameSortKey;

	bool _unreadMark = false;
	bool _fakeUnreadWhileOpened = false;

	// A pointer to the block that is currently being built.
	// We hold this pointer so we can destroy it while building
	// and then create a new one if it is necessary.
	struct BuildingBlock {
		int expectedItemsCount = 0; // optimization for block->items.reserve() call
		HistoryBlock *block = nullptr;
	};
	std::unique_ptr<BuildingBlock> _buildingFrontBlock;

	Data::HistoryDrafts _drafts;
	TimeId _acceptCloudDraftsAfter = 0;
	int _savingCloudDraftRequests = 0;
	MessageIdsList _forwardDraft;

	QString _topPromotedMessage;
	QString _topPromotedType;

	HistoryView::SendActionPainter _sendActionPainter;

	std::deque<not_null<HistoryItem*>> _notifications;

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
