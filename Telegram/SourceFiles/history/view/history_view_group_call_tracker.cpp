/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_group_call_tracker.h"

#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "main/main_session.h"
#include "ui/chat/group_call_bar.h"

namespace HistoryView {

GroupCallTracker::GroupCallTracker(not_null<ChannelData*> channel)
: _channel(channel) {
}

rpl::producer<Ui::GroupCallBarContent> GroupCallTracker::content() const {
	const auto channel = _channel;
	return channel->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::map([=]() -> Ui::GroupCallBarContent {
		const auto call = channel->call();
		if (!call) {
			return { .shown = false };
		} else if (!call->fullCount() && !call->participantsLoaded()) {
			call->requestParticipants();
		}
		return { .count = call->fullCount(), .shown = true };
	});
}

rpl::producer<> GroupCallTracker::joinClicks() const {
	return _joinClicks.events();
}

} // namespace HistoryView
