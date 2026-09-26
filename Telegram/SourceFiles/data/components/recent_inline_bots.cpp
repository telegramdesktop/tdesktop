/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_inline_bots.h"

#include "config.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kLimit = 10;

} // namespace

RecentInlineBots::RecentInlineBots(not_null<Main::Session*> session)
: _session(session) {
}

const std::vector<not_null<UserData*>> &RecentInlineBots::list() const {
	_session->local().readRecentHashtagsAndBots();
	return _list;
}

rpl::producer<> RecentInlineBots::updates() const {
	return _updates.events();
}

void RecentInlineBots::bump(not_null<UserData*> user) {
	_session->local().readRecentHashtagsAndBots();

	if (!_list.empty() && _list.front() == user) {
		return;
	}
	auto i = ranges::find(_list, user);
	if (i == end(_list)) {
		if (int(_list.size()) >= kLimit) {
			_list.pop_back();
		}
		_list.insert(begin(_list), user);
	} else {
		ranges::rotate(begin(_list), i, i + 1);
	}
	_updates.fire({});

	_session->local().writeRecentHashtagsAndBots();
}

void RecentInlineBots::remove(not_null<UserData*> user) {
	const auto i = ranges::find(_list, user);
	if (i != end(_list)) {
		_list.erase(i);
		_updates.fire({});
		_session->local().writeRecentHashtagsAndBots();
	}
}

void RecentInlineBots::applyLocal(
		std::vector<not_null<UserData*>> list) {
	_list = std::move(list);
}

} // namespace Data
