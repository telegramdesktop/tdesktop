/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

class ChannelAdminChanges {
public:
	ChannelAdminChanges(not_null<ChannelData*> channel);

	void feed(UserId userId, bool isAdmin);

	~ChannelAdminChanges();

private:
	not_null<ChannelData*> _channel;
	base::flat_set<UserId> &_admins;
	base::flat_map<UserId, bool> _changes;

};

} // namespace Data
