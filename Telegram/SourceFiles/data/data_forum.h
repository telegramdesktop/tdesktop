/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_main_list.h"

class History;
class ChannelData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window;

namespace Data {

class Session;

struct ForumOffsets {
	TimeId date = 0;
	MsgId id = 0;
	MsgId topicId = 0;

	friend inline constexpr auto operator<=>(
		ForumOffsets,
		ForumOffsets) = default;
};

class Forum final {
public:
	explicit Forum(not_null<History*> history);
	~Forum();

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] not_null<ChannelData*> channel() const;
	[[nodiscard]] not_null<Dialogs::MainList*> topicsList();
	[[nodiscard]] rpl::producer<> destroyed() const;
	[[nodiscard]] auto topicDestroyed() const
		-> rpl::producer<not_null<ForumTopic*>>;

	void preloadTopics();
	void reloadTopics();
	void requestTopics();
	[[nodiscard]] rpl::producer<> chatsListChanges() const;
	[[nodiscard]] rpl::producer<> chatsListLoadedEvents() const;
	void unpinTopic();

	void requestTopic(MsgId rootId, Fn<void()> done = nullptr);
	ForumTopic *applyTopicAdded(
		MsgId rootId,
		const QString &title,
		int32 colorId,
		DocumentId iconId);
	void applyTopicCreated(MsgId rootId, MsgId realId);
	void applyTopicDeleted(MsgId rootId);
	[[nodiscard]] ForumTopic *topicFor(MsgId rootId);
	[[nodiscard]] ForumTopic *enforceTopicFor(MsgId rootId);
	[[nodiscard]] bool topicDeleted(MsgId rootId) const;

	void applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		ForumOffsets &updateOffsets);
	void applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		Fn<void(not_null<ForumTopic*>)> callback = nullptr);

	[[nodiscard]] MsgId reserveCreatingId(
		const QString &title,
		int32 colorId,
		DocumentId iconId);
	void discardCreatingId(MsgId rootId);
	[[nodiscard]] bool creating(MsgId rootId) const;
	void created(MsgId rootId, MsgId realId);

	void clearAllUnreadMentions();
	void clearAllUnreadReactions();
	void enumerateTopics(Fn<void(not_null<ForumTopic*>)> action) const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct TopicRequest {
		mtpRequestId id = 0;
		std::vector<Fn<void()>> callbacks;
	};

	void requestSomeStale();
	void finishTopicRequest(MsgId rootId);

	const not_null<History*> _history;

	base::flat_map<MsgId, std::unique_ptr<ForumTopic>> _topics;
	base::flat_set<MsgId> _topicsDeleted;
	rpl::event_stream<not_null<ForumTopic*>> _topicDestroyed;
	Dialogs::MainList _topicsList;

	base::flat_map<MsgId, TopicRequest> _topicRequests;
	base::flat_set<MsgId> _staleRootIds;
	mtpRequestId _staleRequestId = 0;

	mtpRequestId _requestId = 0;
	ForumOffsets _offset;

	base::flat_set<MsgId> _creatingRootIds;

	rpl::event_stream<> _chatsListChanges;
	rpl::event_stream<> _chatsListLoadedEvents;

	rpl::lifetime _lifetime;

};

} // namespace Data
