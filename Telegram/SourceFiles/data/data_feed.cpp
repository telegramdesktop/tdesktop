/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_feed.h"

namespace Data {

Feed::Feed(FeedId id) : _id(id) {
}

void Feed::registerOne(not_null<ChannelData*> channel) {
	_channels.emplace(channel);
}

void Feed::unregisterOne(not_null<ChannelData*> channel) {
	_channels.remove(channel);
}

void Feed::setUnreadCounts(int unreadCount, int unreadMutedCount) {
	_unreadCount = unreadCount;
	_unreadMutedCount = unreadMutedCount;
}

} // namespace Data
