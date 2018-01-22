/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_feed.h"

#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"

namespace Data {

MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position) {
	Expects(position.type() == mtpc_feedPosition);

	const auto &data = position.c_feedPosition();
	return MessagePosition(data.vdate.v, FullMsgId(
		peerToChannel(peerFromMTP(data.vpeer)),
		data.vid.v));
}

Feed::Feed(FeedId id)
: Entry(this)
, _id(id)
, _name(lang(lng_feed_name)) {
	indexNameParts();
}

void Feed::indexNameParts() {
	_nameWords.clear();
	_nameFirstLetters.clear();
	auto toIndexList = QStringList();
	auto appendToIndex = [&](const QString &value) {
		if (!value.isEmpty()) {
			toIndexList.push_back(TextUtilities::RemoveAccents(value));
		}
	};

	appendToIndex(_name);
	const auto appendTranslit = !toIndexList.isEmpty()
		&& cRussianLetters().match(toIndexList.front()).hasMatch();
	if (appendTranslit) {
		appendToIndex(translitRusEng(toIndexList.front()));
	}
	auto toIndex = toIndexList.join(' ');
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	const auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	for (const auto &name : namesList) {
		_nameWords.insert(name);
		_nameFirstLetters.insert(name[0]);
	}
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
	if (_lastMessage && item->position() <= _lastMessage->position()) {
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

} // namespace Data
