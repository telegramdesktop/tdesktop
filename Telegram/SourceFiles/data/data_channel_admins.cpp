/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "data/data_channel_admins.h"

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
