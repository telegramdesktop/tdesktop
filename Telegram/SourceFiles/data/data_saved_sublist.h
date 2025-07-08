/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_thread.h"
#include "dialogs/ui/dialogs_message_view.h"

class PeerData;
class History;

namespace Data {

class Session;
class Histories;
class SavedMessages;
struct MessagePosition;
struct MessageUpdate;
struct SublistReadTillUpdate;
struct MessagesSlice;

class SavedSublist final : public Data::Thread {
public:
	SavedSublist(
		not_null<SavedMessages*> parent,
		not_null<PeerData*> sublistPeer);
	~SavedSublist();

	[[nodiscard]] bool inMonoforum() const;
	[[nodiscard]] bool isFeeRemoved() const;
	void toggleFeeRemoved(bool feeRemoved);

	void apply(const SublistReadTillUpdate &update);
	void apply(const MessageUpdate &update);
	void applyDifferenceTooLong();
	bool removeOne(not_null<HistoryItem*> item);

	[[nodiscard]] rpl::producer<MessagesSlice> source(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter);

	[[nodiscard]] not_null<SavedMessages*> parent() const;
	[[nodiscard]] not_null<History*> owningHistory() override;
	[[nodiscard]] ChannelData *parentChat() const;
	[[nodiscard]] not_null<PeerData*> sublistPeer() const;
	[[nodiscard]] bool isHiddenAuthor() const;
	[[nodiscard]] rpl::producer<> destroyed() const;

	void growLastKnownServerMessageId(MsgId id);
	void applyMaybeLast(not_null<HistoryItem*> item, bool added = false);
	void applyItemAdded(not_null<HistoryItem*> item);
	void applyItemRemoved(MsgId id);

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] std::optional<int> fullCount() const;
	[[nodiscard]] rpl::producer<int> fullCountValue() const;
	void loadFullCount();

	[[nodiscard]] bool unreadCountKnown() const;
	[[nodiscard]] int unreadCountCurrent() const;
	[[nodiscard]] int displayedUnreadCount() const;
	[[nodiscard]] rpl::producer<std::optional<int>> unreadCountValue() const;
	void setUnreadMark(bool unread);

	void applyMonoforumDialog(
		const MTPDmonoForumDialog &dialog,
		not_null<HistoryItem*> topItem);
	void readTillEnd();
	void requestChatListMessage();

	TimeId adjustedChatListTimeId() const override;

	int fixedOnTopIndex() const override;
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

	void hasUnreadMentionChanged(bool has) override;
	void hasUnreadReactionChanged(bool has) override;

	[[nodiscard]] HistoryItem *lastMessage() const;
	[[nodiscard]] HistoryItem *lastServerMessage() const;
	[[nodiscard]] bool lastMessageKnown() const;
	[[nodiscard]] bool lastServerMessageKnown() const;
	[[nodiscard]] MsgId lastKnownServerMessageId() const;

	void setInboxReadTill(MsgId readTillId, std::optional<int> unreadCount);
	[[nodiscard]] MsgId inboxReadTillId() const;
	[[nodiscard]] MsgId computeInboxReadTillFull() const;

	void setOutboxReadTill(MsgId readTillId);
	[[nodiscard]] MsgId computeOutboxReadTillFull() const;

	[[nodiscard]] bool isServerSideUnread(
		not_null<const HistoryItem*> item) const override;

	void requestUnreadCount();

	void readTill(not_null<HistoryItem*> item);
	void readTill(MsgId tillId);

	void chatListPreloadData() override;
	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const override;

	[[nodiscard]] auto sendActionPainter()
		-> HistoryView::SendActionPainter* override;

private:
	struct Viewer;

	enum class Flag : uchar {
		ResolveChatListMessage = (1 << 0),
		InMonoforum = (1 << 1),
		FeeRemoved = (1 << 2),
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }
	using Flags = base::flags<Flag>;

	[[nodiscard]] Histories &histories();

	void subscribeToUnreadChanges();
	[[nodiscard]] Dialogs::UnreadState unreadStateFor(
		int count,
		bool known) const;
	void setLastMessage(HistoryItem *item);
	void setLastServerMessage(HistoryItem *item);
	void setChatListMessage(HistoryItem *item);
	void allowChatListMessageResolve();
	void resolveChatListMessageGroup();

	void changeUnreadCountByMessage(MsgId id, int delta);
	void setUnreadCount(std::optional<int> count);
	void readTill(MsgId tillId, HistoryItem *tillIdItem);
	void checkReadTillEnd();
	void sendReadTillRequest();
	void reloadUnreadCountIfNeeded();

	[[nodiscard]] bool buildFromData(not_null<Viewer*> viewer);
	[[nodiscard]] bool applyUpdate(const MessageUpdate &update);
	void appendClientSideMessages(MessagesSlice &slice);
	[[nodiscard]] std::optional<int> computeUnreadCountLocally(
		MsgId afterId) const;
	bool processMessagesIsEmpty(const MTPmessages_Messages &result);
	void loadAround(MsgId id);
	void loadBefore();
	void loadAfter();

	const not_null<SavedMessages*> _parent;
	const not_null<History*> _sublistHistory;

	MsgId _lastKnownServerMessageId = 0;

	std::vector<MsgId> _list;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;
	rpl::variable<std::optional<int>> _fullCount;
	rpl::event_stream<> _listChanges;
	rpl::event_stream<> _instantChanges;
	std::optional<MsgId> _loadingAround;
	rpl::variable<std::optional<int>> _unreadCount;
	MsgId _inboxReadTillId = 0;
	MsgId _outboxReadTillId = 0;
	Flags _flags;

	std::optional<HistoryItem*> _lastMessage;
	std::optional<HistoryItem*> _lastServerMessage;
	std::optional<HistoryItem*> _chatListMessage;
	base::flat_set<FullMsgId> _requestedGroups;
	int _beforeId = 0;
	int _afterId = 0;

	base::Timer _readRequestTimer;
	mtpRequestId _readRequestId = 0;
	MsgId _sentReadTill = 0;

	mtpRequestId _reloadUnreadCountRequestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
