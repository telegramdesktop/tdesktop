/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat_filters.h"

#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"

namespace Data {

ChatFilter::ChatFilter(
	const QString &title,
	Flags flags,
	base::flat_set<not_null<History*>> always)
: _title(title)
, _always(std::move(always))
, _flags(flags) {
}

QString ChatFilter::title() const {
	return _title;
}

bool ChatFilter::contains(not_null<History*> history) const {
	const auto flag = [&] {
		const auto peer = history->peer;
		if (const auto user = peer->asUser()) {
			return user->isBot() ? Flag::Bots : Flag::Users;
		} else if (const auto chat = peer->asChat()) {
			return Flag::PrivateGroups;
		} else if (const auto channel = peer->asChannel()) {
			if (channel->isBroadcast()) {
				return Flag::Broadcasts;
			} else if (channel->isPublic()) {
				return Flag::PublicGroups;
			} else {
				return Flag::PrivateGroups;
			}
		} else {
			Unexpected("Peer type in ChatFilter::contains.");
		}
	}();
	if (history->folder()) {
		return false;
	}
	return false
		|| ((_flags & flag)
			&& (!(_flags & Flag::NoMuted) || !history->mute())
			&& (!(_flags & Flag::NoRead) || history->unreadCountForBadge()))
		|| _always.contains(history);
}

ChatFilters::ChatFilters(not_null<Session*> owner) : _owner(owner) {
	using Flag = ChatFilter::Flag;
	const auto all = Flag::Users
		| Flag::SecretChats
		| Flag::PrivateGroups
		| Flag::PublicGroups
		| Flag::Broadcasts
		| Flag::Bots;
	_list.emplace(
		1,
		ChatFilter("Unmuted Chats", all | ChatFilter::Flag::NoMuted, {}));
	_list.emplace(
		2,
		ChatFilter("Unread Chats", all | ChatFilter::Flag::NoRead, {}));
}

const base::flat_map<FilterId, ChatFilter> &ChatFilters::list() const {
	return _list;
}

void ChatFilters::refreshHistory(not_null<History*> history) {
	_refreshHistoryRequests.fire_copy(history);
}

auto ChatFilters::refreshHistoryRequests() const
-> rpl::producer<not_null<History*>> {
	return _refreshHistoryRequests.events();
}


} // namespace Data
