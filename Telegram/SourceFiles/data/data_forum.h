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

namespace Window {
class SessionController;
} // namespace Window;

namespace Data {

class Forum final {
public:
	explicit Forum(not_null<History*> history);
	~Forum();

	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] not_null<ChannelData*> channel() const;
	[[nodiscard]] not_null<Dialogs::MainList*> topicsList();

	void requestTopics();
	[[nodiscard]] rpl::producer<> chatsListChanges() const;
	[[nodiscard]] rpl::producer<> chatsListLoadedEvents() const;

	void applyTopicAdded(
		MsgId rootId,
		const QString &title,
		int32 colorId,
		DocumentId iconId);
	void applyTopicRemoved(MsgId rootId);
	void applyTopicCreated(MsgId rootId, MsgId realId);
	[[nodiscard]] ForumTopic *topicFor(not_null<HistoryItem*> item);
	[[nodiscard]] ForumTopic *topicFor(MsgId rootId);

	void applyReceivedTopics(const MTPmessages_ForumTopics &topics);

	[[nodiscard]] MsgId reserveCreatingId(
		const QString &title,
		int32 colorId,
		DocumentId iconId);
	void discardCreatingId(MsgId rootId);
	[[nodiscard]] bool creating(MsgId rootId) const;
	void created(MsgId rootId, MsgId realId);

private:
	void applyReceivedTopics(
		const MTPmessages_ForumTopics &topics,
		bool updateOffset);

	const not_null<History*> _history;

	base::flat_map<MsgId, std::unique_ptr<ForumTopic>> _topics;
	Dialogs::MainList _topicsList;

	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	MsgId _offsetTopicId = 0;
	bool _allLoaded = false;

	base::flat_set<MsgId> _creatingRootIds;

	rpl::event_stream<> _chatsListChanges;
	rpl::event_stream<> _chatsListLoadedEvents;

};

} // namespace Data
