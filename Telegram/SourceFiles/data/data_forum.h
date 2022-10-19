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

	void applyReceivedTopics(const MTPmessages_ForumTopics &topics);

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

	void applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		bool updateOffset);

	const not_null<History*> _history;

	base::flat_map<MsgId, std::unique_ptr<ForumTopic>> _topics;
	rpl::event_stream<not_null<ForumTopic*>> _topicDestroyed;
	Dialogs::MainList _topicsList;

	base::flat_map<MsgId, TopicRequest> _topicRequests;

	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	MsgId _offsetTopicId = 0;
	bool _allLoaded = false;

	base::flat_set<MsgId> _creatingRootIds;

	rpl::event_stream<> _chatsListChanges;
	rpl::event_stream<> _chatsListLoadedEvents;

	rpl::lifetime _lifetime;

};

} // namespace Data
