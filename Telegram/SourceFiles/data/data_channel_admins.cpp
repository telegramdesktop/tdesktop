/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_channel_admins.h"

#include "history/history.h"

namespace Data {

ChannelAdminChanges::ChannelAdminChanges(not_null<ChannelData*> channel)
: _channel(channel)
, _admins(_channel->mgInfo->admins) {
}

void ChannelAdminChanges::feed(UserId userId, bool isAdmin) {
	if (isAdmin && !_admins.contains(userId)) {
		_admins.insert(userId);
		_changes.emplace(userId, true);
	} else if (!isAdmin && _admins.contains(userId)) {
		_admins.remove(userId);
		_changes.emplace(userId, false);
	}
}

ChannelAdminChanges::~ChannelAdminChanges() {
	if (!_changes.empty()) {
		if (auto history = App::historyLoaded(_channel)) {
			history->applyGroupAdminChanges(_changes);
		}
	}
}

} // namespace Data
