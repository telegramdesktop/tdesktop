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
#include "data/data_thread.h"
#include "history/view/history_view_send_action.h"
#include "base/variant.h"
#include "base/flat_set.h"
#include "base/flags.h"

class History;
class HistoryBlock;
class HistoryTranslation;
class HistoryItem;
struct HistoryItemCommonFields;
struct HistoryMessageMarkupData;
class HistoryMainElementDelegateMixin;
struct LanguageId;

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct Draft;
class Session;
class Folder;
class ChatFilter;
struct SponsoredFrom;
class SponsoredMessages;

enum class ForwardOptions {
	PreserveInfo,
	NoSenderNames,
	NoNamesAndCaptions,
};

struct ForwardDraft {
	MessageIdsList ids;
	ForwardOptions options = ForwardOptions::PreserveInfo;

	friend inline auto operator<=>(
		const ForwardDraft&,
		const ForwardDraft&) = default;
};

using ForwardDrafts = base::flat_map<MsgId, ForwardDraft>;

struct ResolvedForwardDraft {
	HistoryItemsList items;
	ForwardOptions options = ForwardOptions::PreserveInfo;
};

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

class History final : public Data::Thread {
public:
	using Element = HistoryView::Element;

	History(not_null<Data::Session*> owner, PeerId peerId);
	~History();

	not_null<History*> owningHistory() override {
		return this;
	}
	[[nodiscard]] Data::Thread *threadFor(MsgId topicRootId);
	[[nodiscard]] const Data::Thread *threadFor(MsgId topicRootId) const;

	[[nodiscard]] auto delegateMixin() const
			-> not_null<HistoryMainElementDelegateMixin*> {
		return _delegateMixin.get();
	}

	void forumChanged(Data::Forum *old);
	[[nodiscard]] bool isForum() const;

	not_null<History*> migrateToOrMe() const;
	History *migrateFrom() const;
	MsgRange rangeForDifferenceRequest() const;

	HistoryItem *joinedMessageInstance() const;
	void checkLocalMessages();
	void removeJoinedMessage();

	void reactionsEnabledChanged(bool enabled);

	bool isEmpty() const;
	bool isDisplayedEmpty() const;
	Element *findFirstNonEmpty() const;
	Element *findFirstDisplayed() const;
	Element *findLastNonEmpty() const;
	Element *findLastDisplayed() const;
	bool hasOrphanMediaGroupPart() const;
	[[nodiscard]] std::vector<MsgId> collectMessagesFromParticipantToDelete(
		not_null<PeerData*> participant) const;

	enum class ClearType {
		Unload,
		DeleteChat,
		ClearHistory,
	};
	void clear(ClearType type);
	void clearUpTill(MsgId availableMinId);

	void applyGroupAdminChanges(const base::flat_set<UserId> &changes);

	template <typename ...Args>
	not_null<HistoryItem*> makeMessage(MsgId id, Args &&...args) {
		return static_cast<HistoryItem*>(
			insertItem(
				std::make_unique<HistoryItem>(
					this,
					id,
					std::forward<Args>(args)...)).get());
	}
	template <typename ...Args>
	not_null<HistoryItem*> makeMessage(
			HistoryItemCommonFields &&fields,
			Args &&...args) {
		return static_cast<HistoryItem*>(
			insertItem(
				std::make_unique<HistoryItem>(
					this,
					std::move(fields),
					std::forward<Args>(args)...)).get());
	}

	void destroyMessage(not_null<HistoryItem*> item);
	void destroyMessagesByDates(TimeId minDate, TimeId maxDate);
	void destroyMessagesByTopic(MsgId topicRootId);

	void unpinMessagesFor(MsgId topicRootId);

	not_null<HistoryItem*> addNewMessage(
		MsgId id,
		const MTPMessage &message,
		MessageFlags localFlags,
		NewMessageType type);

	not_null<HistoryItem*> addNewLocalMessage(
		HistoryItemCommonFields &&fields,
		const TextWithEntities &text,
		const MTPMessageMedia &media);
	not_null<HistoryItem*> addNewLocalMessage(
		HistoryItemCommonFields &&fields,
		not_null<HistoryItem*> forwardOriginal);
	not_null<HistoryItem*> addNewLocalMessage(
		HistoryItemCommonFields &&fields,
		not_null<DocumentData*> document,
		const TextWithEntities &caption);
	not_null<HistoryItem*> addNewLocalMessage(
		HistoryItemCommonFields &&fields,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption);
	not_null<HistoryItem*> addNewLocalMessage(
		HistoryItemCommonFields &&fields,
		not_null<GameData*> game);

	not_null<HistoryItem*> addSponsoredMessage(
		MsgId id,
		Data::SponsoredFrom from,
		const TextWithEntities &textWithEntities); // sponsored

	// Used only internally and for channel admin log.
	not_null<HistoryItem*> createItem(
		MsgId id,
		const MTPMessage &message,
		MessageFlags localFlags,
		bool detachExistingItem);
	std::vector<not_null<HistoryItem*>> createItems(
		const QVector<MTPMessage> &data);

	void addOlderSlice(const QVector<MTPMessage> &slice);
	void addNewerSlice(const QVector<MTPMessage> &slice);

	void newItemAdded(not_null<HistoryItem*> item);

	void registerClientSideMessage(not_null<HistoryItem*> item);
	void unregisterClientSideMessage(not_null<HistoryItem*> item);
	[[nodiscard]] auto clientSideMessages()
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
	[[nodiscard]] MsgId loadAroundId() const;
	[[nodiscard]] MsgId inboxReadTillId() const;
	[[nodiscard]] MsgId outboxReadTillId() const;

	[[nodiscard]] bool isServerSideUnread(
		not_null<const HistoryItem*> item) const override;

	[[nodiscard]] bool trackUnreadMessages() const;
	[[nodiscard]] int unreadCount() const;
	[[nodiscard]] bool unreadCountKnown() const;

	// Some old unread count is known, but we read history till some place.
	[[nodiscard]] bool unreadCountRefreshNeeded(MsgId readTillId) const;

	void setUnreadCount(int newUnreadCount);
	void setUnreadMark(bool unread);
	void setFakeUnreadWhileOpened(bool enabled);
	[[nodiscard]] bool fakeUnreadWhileOpened() const;
	void setMuted(bool muted) override;
	void addUnreadBar();
	void destroyUnreadBar();
	[[nodiscard]] Element *unreadBar() const;
	void calculateFirstUnreadMessage();
	void unsetFirstUnreadMessage();
	[[nodiscard]] Element *firstUnreadMessage() const;

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
	[[nodiscard]] QStringView topPromotionType() const;
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

	bool hasPendingResizedItems() const;
	void setHasPendingResizedItems();

	[[nodiscard]] auto sendActionPainter()
	-> not_null<HistoryView::SendActionPainter*> override {
		return &_sendActionPainter;
	}

	void clearLastKeyboard();
	void clearUnreadMentionsFor(MsgId topicRootId);
	void clearUnreadReactionsFor(MsgId topicRootId);

	Data::Draft *draft(Data::DraftKey key) const;
	void setDraft(Data::DraftKey key, std::unique_ptr<Data::Draft> &&draft);
	void clearDraft(Data::DraftKey key);

	[[nodiscard]] const Data::HistoryDrafts &draftsMap() const;
	void setDraftsMap(Data::HistoryDrafts &&map);

	Data::Draft *localDraft(MsgId topicRootId) const {
		return draft(Data::DraftKey::Local(topicRootId));
	}
	Data::Draft *localEditDraft(MsgId topicRootId) const {
		return draft(Data::DraftKey::LocalEdit(topicRootId));
	}
	Data::Draft *cloudDraft(MsgId topicRootId) const {
		return draft(Data::DraftKey::Cloud(topicRootId));
	}
	void setLocalDraft(std::unique_ptr<Data::Draft> &&draft) {
		setDraft(
			Data::DraftKey::Local(draft->reply.topicRootId),
			std::move(draft));
	}
	void setLocalEditDraft(std::unique_ptr<Data::Draft> &&draft) {
		setDraft(
			Data::DraftKey::LocalEdit(draft->reply.topicRootId),
			std::move(draft));
	}
	void setCloudDraft(std::unique_ptr<Data::Draft> &&draft) {
		setDraft(
			Data::DraftKey::Cloud(draft->reply.topicRootId),
			std::move(draft));
	}
	void clearLocalDraft(MsgId topicRootId) {
		clearDraft(Data::DraftKey::Local(topicRootId));
	}
	void clearCloudDraft(MsgId topicRootId) {
		clearDraft(Data::DraftKey::Cloud(topicRootId));
	}
	void clearLocalEditDraft(MsgId topicRootId) {
		clearDraft(Data::DraftKey::LocalEdit(topicRootId));
	}
	void clearDrafts();
	Data::Draft *createCloudDraft(
		MsgId topicRootId,
		const Data::Draft *fromDraft);
	[[nodiscard]] bool skipCloudDraftUpdate(
		MsgId topicRootId,
		TimeId date) const;
	void startSavingCloudDraft(MsgId topicRootId);
	void finishSavingCloudDraft(MsgId topicRootId, TimeId savedAt);
	void takeLocalDraft(not_null<History*> from);
	void applyCloudDraft(MsgId topicRootId);
	void draftSavedToCloud(MsgId topicRootId);
	void requestChatListMessage();

	[[nodiscard]] const Data::ForwardDraft &forwardDraft(
		MsgId topicRootId) const;
	[[nodiscard]] Data::ResolvedForwardDraft resolveForwardDraft(
		const Data::ForwardDraft &draft) const;
	[[nodiscard]] Data::ResolvedForwardDraft resolveForwardDraft(
		MsgId topicRootId);
	void setForwardDraft(MsgId topicRootId, Data::ForwardDraft &&draft);

	History *migrateSibling() const;
	[[nodiscard]] bool useTopPromotion() const;
	int fixedOnTopIndex() const override;
	void updateChatListExistence() override;
	bool shouldBeInChatList() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	Dialogs::BadgesState chatListBadgesState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	int chatListNameVersion() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;
	void chatListPreloadData() override;
	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const override;

	void refreshChatListNameSortKey();

	void setFakeChatListMessageFrom(const MTPmessages_Messages &data);
	void checkChatListMessageRemoved(not_null<HistoryItem*> item);

	void applyChatListGroup(
		PeerId dataPeerId,
		const MTPmessages_Messages &data);

	void forgetScrollState() {
		scrollTopItem = nullptr;
	}

	// find the correct scrollTopItem and scrollTopOffset using given top
	// of the displayed window relative to the history start coordinate
	void countScrollState(int top);

	[[nodiscard]] std::pair<Element*, int> findItemAndOffset(int top) const;

	[[nodiscard]] MsgId nextNonHistoryEntryId();

	bool folderKnown() const override;
	Data::Folder *folder() const override;
	void setFolder(
		not_null<Data::Folder*> folder,
		HistoryItem *folderDialogItem = nullptr);
	void clearFolder();

	// Interface for Data::Histories.
	void setInboxReadTill(MsgId upTo);
	std::optional<int> countStillUnreadLocal(MsgId readTillId) const;

	[[nodiscard]] bool isTopPromoted() const;

	void translateOfferFrom(LanguageId id);
	[[nodiscard]] LanguageId translateOfferedFrom() const;
	void translateTo(LanguageId id);
	[[nodiscard]] LanguageId translatedTo() const;

	[[nodiscard]] HistoryTranslation *translation() const;

	const not_null<PeerData*> peer;

	// Still public data.
	std::deque<std::unique_ptr<HistoryBlock>> blocks;

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

private:
	friend class HistoryBlock;

	enum class Flag : uchar {
		HasPendingResizedItems = (1 << 0),
		PendingAllItemsResize = (1 << 1),
		IsTopPromoted = (1 << 2),
		IsForum = (1 << 3),
		FakeUnreadWhileOpened = (1 << 4),
		HasPinnedMessages = (1 << 5),
		ResolveChatListMessage = (1 << 6),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) {
		return true;
	};

	void cacheTopPromoted(bool promoted);

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

	friend class Data::SponsoredMessages;
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

	void addCreatedOlderSlice(
		const std::vector<not_null<HistoryItem*>> &items);

	void checkForLoadedAtTop(not_null<HistoryItem*> added);
	void mainViewRemoved(
		not_null<HistoryBlock*> block,
		not_null<Element*> view);

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
	void allowChatListMessageResolve();
	void resolveChatListMessageGroup();

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

	void createLocalDraftFromCloud(MsgId topicRootId);

	HistoryItem *insertJoinedMessage();
	void insertMessageToBlocks(not_null<HistoryItem*> item);

	[[nodiscard]] Dialogs::BadgesState computeBadgesState() const;
	[[nodiscard]] Dialogs::BadgesState adjustBadgesStateByFolder(
		Dialogs::BadgesState state) const;
	[[nodiscard]] Dialogs::UnreadState computeUnreadState() const;
	void setFolderPointer(Data::Folder *folder);

	void hasUnreadMentionChanged(bool has) override;
	void hasUnreadReactionChanged(bool has) override;

	const std::unique_ptr<HistoryMainElementDelegateMixin> _delegateMixin;

	Flags _flags = 0;
	int _width = 0;
	int _height = 0;
	Element *_unreadBarView = nullptr;
	Element *_firstUnreadView = nullptr;
	HistoryItem *_joinedMessage = nullptr;
	bool _loadedAtTop = false;
	bool _loadedAtBottom = true;

	std::optional<Data::Folder*> _folder;

	std::optional<MsgId> _inboxReadBefore;
	std::optional<MsgId> _outboxReadBefore;
	std::optional<int> _unreadCount;
	std::optional<HistoryItem*> _lastMessage;
	std::optional<HistoryItem*> _lastServerMessage;
	base::flat_set<not_null<HistoryItem*>> _clientSideMessages;
	std::unordered_set<std::unique_ptr<HistoryItem>> _messages;

	// This almost always is equal to _lastMessage. The only difference is
	// for a group that migrated to a supergroup. Then _lastMessage can
	// be a migrate message, but _chatListMessage should be the one before.
	std::optional<HistoryItem*> _chatListMessage;

	QString _chatListNameSortKey;

	// A pointer to the block that is currently being built.
	// We hold this pointer so we can destroy it while building
	// and then create a new one if it is necessary.
	struct BuildingBlock {
		int expectedItemsCount = 0; // optimization for block->items.reserve() call
		HistoryBlock *block = nullptr;
	};
	std::unique_ptr<BuildingBlock> _buildingFrontBlock;
	std::unique_ptr<HistoryTranslation> _translation;

	Data::HistoryDrafts _drafts;
	base::flat_map<MsgId, TimeId> _acceptCloudDraftsAfter;
	base::flat_map<MsgId, int> _savingCloudDraftRequests;
	Data::ForwardDrafts _forwardDrafts;

	QString _topPromotedMessage;
	QString _topPromotedType;

	HistoryView::SendActionPainter _sendActionPainter;


};

class HistoryBlock {
public:
	using Element = HistoryView::Element;

	enum class ResizeRequest {
		ReinitAll = 0,
		ResizeAll = 1,
		ResizePending = 2,
	};

	HistoryBlock(not_null<History*> history);
	HistoryBlock(const HistoryBlock &) = delete;
	HistoryBlock &operator=(const HistoryBlock &) = delete;
	~HistoryBlock();

	std::vector<std::unique_ptr<Element>> messages;

	void remove(not_null<Element*> view);
	void refreshView(not_null<Element*> view);

	int resizeGetHeight(int newWidth, ResizeRequest request);
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
