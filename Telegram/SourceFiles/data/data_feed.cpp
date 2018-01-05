/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_feed.h"

#include "dialogs/dialogs_key.h"

namespace Data {

FeedPosition::FeedPosition(const MTPFeedPosition &position)
: date(position.c_feedPosition().vdate.v)
, peerId(peerFromMTP(position.c_feedPosition().vpeer))
, msgId(position.c_feedPosition().vid.v) {
}

FeedPosition::FeedPosition(not_null<HistoryItem*> item)
: date(toServerTime(item->date.toTime_t()).v)
, peerId(item->history()->peer->id)
, msgId(item->id) {
}

bool FeedPosition::operator<(const FeedPosition &other) const {
	if (date < other.date) {
		return true;
	} else if (other.date < date) {
		return false;
	}
	const auto peer = peerToBareInt(peerId);
	const auto otherPeer = peerToBareInt(other.peerId);
	if (peer < otherPeer) {
		return true;
	} else if (otherPeer < peer) {
		return false;
	}
	return (msgId < other.msgId);
}

Feed::Feed(FeedId id)
: Entry(this)
, _id(id) {
}

void Feed::registerOne(not_null<ChannelData*> channel) {
	const auto history = App::history(channel);
	if (!base::contains(_channels, history)) {
		_channels.push_back(history);
		if (history->lastMsg) {
			updateLastMessage(history->lastMsg);
		}
	}
}

void Feed::unregisterOne(not_null<ChannelData*> channel) {
	const auto history = App::history(channel);
	_channels.erase(ranges::remove(_channels, history), end(_channels));
	if (_lastMessage->history() == history) {
		messageRemoved(_lastMessage);
	}
}

void Feed::updateLastMessage(not_null<HistoryItem*> item) {
	if (justSetLastMessage(item)) {
		setChatsListDate(_lastMessage->date);
	}
}

void Feed::loadUserpic() {
	constexpr auto kPaintUserpicsCount = 4;
	auto load = kPaintUserpicsCount;
	for (const auto channel : _channels) {
		channel->peer->loadUserpic();
		if (!--load) {
			break;
		}
	}
}

void Feed::paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const {
	const auto small = (size - st::lineWidth) / 2;
	const auto delta = size - small;
	auto index = 0;
	for (const auto channel : _channels) {
		channel->peer->paintUserpic(p, x, y, small);
		switch (++index) {
		case 1:
		case 3: x += delta; break;
		case 2: x -= delta; y += delta; break;
		}
	}
}

bool Feed::justSetLastMessage(not_null<HistoryItem*> item) {
	if (_lastMessage && FeedPosition(item) <= FeedPosition(_lastMessage)) {
		return false;
	}
	_lastMessage = item;
	return true;
}

void Feed::messageRemoved(not_null<HistoryItem*> item) {
	if (_lastMessage == item) {
		recountLastMessage();
	}
}

void Feed::historyCleared(not_null<History*> history) {
	if (_lastMessage->history() == history) {
		recountLastMessage();
	}
}

void Feed::recountLastMessage() {
	_lastMessage = nullptr;
	for (const auto history : _channels) {
		if (const auto last = history->lastMsg) {
			justSetLastMessage(last);
		}
	}
	if (_lastMessage) {
		setChatsListDate(_lastMessage->date);
	}
}

void Feed::setUnreadCounts(int unreadCount, int unreadMutedCount) {
	_unreadCount = unreadCount;
	_unreadMutedCount = unreadMutedCount;
}

void Feed::setUnreadPosition(const FeedPosition &position) {
	_unreadPosition = position;
}

} // namespace Data
