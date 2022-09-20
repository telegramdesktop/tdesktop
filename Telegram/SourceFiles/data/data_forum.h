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
	explicit Forum(not_null<History*> forum);
	~Forum();

	[[nodiscard]] not_null<Dialogs::MainList*> topicsList();

	void requestTopics();
	[[nodiscard]] rpl::producer<> chatsListChanges() const;
	[[nodiscard]] rpl::producer<> chatsListLoadedEvents() const;

	void topicAdded(not_null<HistoryItem*> root);

private:
	const not_null<History*> _forum;

	base::flat_map<MsgId, std::unique_ptr<ForumTopic>> _topics;
	Dialogs::MainList _topicsList;

	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	MsgId _offsetTopicId = 0;
	bool _allLoaded = false;

	rpl::event_stream<> _chatsListChanges;
	rpl::event_stream<> _chatsListLoadedEvents;

};

void ShowAddForumTopic(
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> forum);

} // namespace Data
