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
#include "data/data_user.h"
#include "main/main_session.h"

namespace Data {

ChannelAdminChanges::ChannelAdminChanges(not_null<ChannelData*> channel)
: _channel(channel)
, _admins(_channel->mgInfo->admins)
, _oldCreator(_channel->mgInfo->creator) {
}

void ChannelAdminChanges::add(UserId userId, const QString &rank) {
	if (_admins.emplace(userId).second) {
		_changes.emplace(userId);
	}
	auto &ranks = _channel->mgInfo->memberRanks;
	if (!rank.isEmpty()) {
		const auto i = ranks.find(userId);
		if (i == end(ranks) || i->second != rank) {
			ranks[userId] = rank;
			_changes.emplace(userId);
		}
	} else {
		if (ranks.remove(userId)) {
			_changes.emplace(userId);
		}
	}
}

void ChannelAdminChanges::remove(UserId userId) {
	if (_admins.remove(userId)) {
		_changes.emplace(userId);
	}
}

ChannelAdminChanges::~ChannelAdminChanges() {
	const auto creator = _channel->mgInfo->creator;
	if (creator != _oldCreator) {
		if (creator) {
			_changes.emplace(peerToUser(creator->id));
		}
		if (_oldCreator) {
			_changes.emplace(peerToUser(_oldCreator->id));
		}
	}
	if (_changes.size() > 1
		|| (!_changes.empty()
			&& _changes.front() != _channel->session().userId())) {
		if (const auto history = _channel->owner().historyLoaded(_channel)) {
			history->applyGroupAdminChanges(_changes);
		}
	}
}

ChannelMemberRankChanges::ChannelMemberRankChanges(
		not_null<ChannelData*> channel)
: _channel(channel)
, _memberRanks(_channel->mgInfo->memberRanks) {
}

void ChannelMemberRankChanges::feed(
		UserId userId,
		const QString &rank) {
	if (rank.isEmpty()) {
		if (_memberRanks.remove(userId)) {
			_changes.emplace(userId);
		}
	} else {
		const auto i = _memberRanks.find(userId);
		if (i == end(_memberRanks) || i->second != rank) {
			_memberRanks[userId] = rank;
			_changes.emplace(userId);
		}
	}
}

ChannelMemberRankChanges::~ChannelMemberRankChanges() {
	if (_changes.empty()) {
		return;
	}
	const auto info = _channel->mgInfo.get();
	auto adminChanges = base::flat_set<UserId>();
	for (const auto &userId : _changes) {
		if (info->admins.contains(userId)
			|| (info->creator
				&& peerToUser(info->creator->id) == userId)) {
			adminChanges.emplace(userId);
		}
	}
	if (adminChanges.size() > 1
		|| (!adminChanges.empty()
			&& adminChanges.front() != _channel->session().userId())) {
		if (const auto history
			= _channel->owner().historyLoaded(_channel)) {
			history->applyGroupAdminChanges(adminChanges);
		}
	}
}

} // namespace Data
