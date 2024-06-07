/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"

class History;

namespace Data {

class ForumTopic;
class Histories;
struct MessagePosition;
struct MessagesSlice;
struct MessageUpdate;
struct TopicUpdate;
struct RepliesReadTillUpdate;

class RepliesList final : public base::has_weak_ptr {
public:
	RepliesList(
		not_null<History*> history,
		MsgId rootId,
		ForumTopic *owningTopic = nullptr);
	~RepliesList();

	void apply(const RepliesReadTillUpdate &update);
	void apply(const MessageUpdate &update);
	void apply(const TopicUpdate &update);
	void applyDifferenceTooLong();

	[[nodiscard]] rpl::producer<MessagesSlice> source(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter);

	[[nodiscard]] rpl::producer<int> fullCount() const;
	[[nodiscard]] rpl::producer<std::optional<int>> maybeFullCount() const;

	[[nodiscard]] bool unreadCountKnown() const;
	[[nodiscard]] int unreadCountCurrent() const;
	[[nodiscard]] int displayedUnreadCount() const;
	[[nodiscard]] rpl::producer<std::optional<int>> unreadCountValue() const;

	void setInboxReadTill(MsgId readTillId, std::optional<int> unreadCount);
	[[nodiscard]] MsgId inboxReadTillId() const;
	[[nodiscard]] MsgId computeInboxReadTillFull() const;

	void setOutboxReadTill(MsgId readTillId);
	[[nodiscard]] MsgId computeOutboxReadTillFull() const;

	[[nodiscard]] bool isServerSideUnread(
		not_null<const HistoryItem*> item) const;

	[[nodiscard]] std::optional<int> computeUnreadCountLocally(
		MsgId afterId) const;
	void requestUnreadCount();

	void readTill(not_null<HistoryItem*> item);
	void readTill(MsgId tillId);

	[[nodiscard]] bool canDeleteMyTopic() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Viewer;

	HistoryItem *lookupRoot();
	[[nodiscard]] Histories &histories();

	void subscribeToUpdates();
	void appendClientSideMessages(MessagesSlice &slice);

	[[nodiscard]] bool buildFromData(not_null<Viewer*> viewer);
	[[nodiscard]] bool applyItemDestroyed(
		not_null<Viewer*> viewer,
		not_null<HistoryItem*> item);
	[[nodiscard]] bool applyUpdate(const MessageUpdate &update);
	void applyTopicCreator(PeerId creatorId);
	void injectRootMessageAndReverse(not_null<Viewer*> viewer);
	void injectRootMessage(not_null<Viewer*> viewer);
	void injectRootDivider(
		not_null<HistoryItem*> root,
		not_null<MessagesSlice*> slice);
	bool processMessagesIsEmpty(const MTPmessages_Messages &result);
	void loadAround(MsgId id);
	void loadBefore();
	void loadAfter();

	void changeUnreadCountByPost(MsgId id, int delta);
	void setUnreadCount(std::optional<int> count);
	void readTill(MsgId tillId, HistoryItem *tillIdItem);
	void checkReadTillEnd();
	void sendReadTillRequest();
	void reloadUnreadCountIfNeeded();

	const not_null<History*> _history;
	ForumTopic *_owningTopic = nullptr;
	const MsgId _rootId = 0;
	const bool _creating = false;

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
	HistoryItem *_divider = nullptr;
	bool _dividerWithComments = false;
	int _beforeId = 0;
	int _afterId = 0;

	base::Timer _readRequestTimer;
	mtpRequestId _readRequestId = 0;

	mtpRequestId _reloadUnreadCountRequestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
