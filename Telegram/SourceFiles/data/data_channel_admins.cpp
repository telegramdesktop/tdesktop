/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_channel_admins.h"

#include "history/history.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Data {

ChannelAdminChanges::ChannelAdminChanges(not_null<ChannelData*> channel)
: _channel(channel)
, _admins(_channel->mgInfo->admins) {
}

void ChannelAdminChanges::add(UserId userId, const QString &rank) {
	const auto i = _admins.find(userId);
	if (i == end(_admins) || i->second != rank) {
		_admins[userId] = rank;
		_changes.emplace(userId);
	}
}

void ChannelAdminChanges::remove(UserId userId) {
	if (_admins.contains(userId)) {
		_admins.remove(userId);
		_changes.emplace(userId);
	}
}

ChannelAdminChanges::~ChannelAdminChanges() {
	if (_changes.size() > 1
		|| (!_changes.empty()
			&& _changes.front() != _channel->session().userId())) {
		if (const auto history = _channel->owner().historyLoaded(_channel)) {
			history->applyGroupAdminChanges(_changes);
		}
	}
}

} // namespace Data
