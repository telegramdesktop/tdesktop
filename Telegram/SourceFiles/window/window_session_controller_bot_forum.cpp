/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_replies_list.h"
#include "history/history.h"

namespace Window {

void SessionNavigation::loadAndShowBotTopic(
		not_null<History*> history,
		MsgId rootId,
		MsgId commentId,
		const SectionShow &params) {
	Expects(history->peer->forum() != nullptr);

	_api.request(base::take(_showingRepliesRequestId)).cancel();
	_showingRepliesHistory = history;
	_showingRepliesRootId = rootId;
	const auto load = ++_botTopicLoads;
	history->peer->forum()->requestTopic(rootId, crl::guard(this, [=] {
		if (_botTopicLoads != load
			|| _showingRepliesHistory != history.get()
			|| _showingRepliesRootId != rootId) {
			return;
		}
		_showingRepliesHistory = nullptr;
		_showingRepliesRootId = 0;
		const auto topic = history->peer->forumTopicFor(rootId);
		if (topic && topic->replies()->unreadCountKnown()) {
			showRepliesForMessage(history, rootId, commentId, params);
		} else {
			showPeerHistory(history, params);
		}
	}));
}

} // namespace Window
