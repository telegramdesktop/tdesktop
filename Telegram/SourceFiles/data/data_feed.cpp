/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_feed.h"

#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "storage/storage_facade.h"
#include "storage/storage_feed_messages.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "mainwidget.h"

namespace Data {

MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position) {
	Expects(position.type() == mtpc_feedPosition);

	const auto &data = position.c_feedPosition();
	return MessagePosition(data.vdate.v, FullMsgId(
		peerToChannel(peerFromMTP(data.vpeer)),
		data.vid.v));
}

Feed::Feed(FeedId id, not_null<Data::Session*> parent)
: Entry(this)
, _id(id)
, _parent(parent)
, _name(lang(lng_feed_name)) {
	indexNameParts();
}

FeedId Feed::id() const {
	return _id;
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
		const auto invisible = (_channels.size() < 2);
		_channels.push_back(history);
		if (history->lastMsg) {
			updateLastMessage(history->lastMsg);
		}
		_parent->session().storage().remove(
			Storage::FeedMessagesInvalidate(_id));

		if (invisible && _channels.size() > 1) {
			updateChatListExistence();
			for (const auto history : _channels) {
				history->updateChatListExistence();
			}
		} else {
			history->updateChatListExistence();
		}
		_parent->notifyFeedUpdated(this, FeedUpdateFlag::Channels);
	}
}

void Feed::unregisterOne(not_null<ChannelData*> channel) {
	const auto history = App::history(channel);
	const auto i = ranges::remove(_channels, history);
	if (i != end(_channels)) {
		const auto visible = (_channels.size() > 1);
		_channels.erase(i, end(_channels));
		if (_lastMessage && _lastMessage->history() == history) {
			messageRemoved(_lastMessage);
		}
		_parent->session().storage().remove(
			Storage::FeedMessagesRemoveAll(_id, channel->bareId()));

		if (visible && _channels.size() < 2) {
			updateChatListExistence();
			for (const auto history : _channels) {
				history->updateChatListExistence();
			}
		} else {
			history->updateChatListExistence();
		}
		_parent->notifyFeedUpdated(this, FeedUpdateFlag::Channels);
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
		case 4: return;
		}
	}
}

const std::vector<not_null<History*>> &Feed::channels() const {
	return _channels;
}

int32 Feed::channelsHash() const {
	return Api::CountHash(ranges::view::all(
		_channels
	) | ranges::view::transform([](not_null<History*> history) {
		return history->peer->bareId();
	}));
}

bool Feed::channelsLoaded() const {
	return _channelsLoaded;
}

void Feed::setChannelsLoaded(bool loaded) {
	_channelsLoaded = loaded;
}

void Feed::setChannels(std::vector<not_null<ChannelData*>> channels) {
	const auto remove = ranges::view::all(
		_channels
	) | ranges::view::transform([](not_null<History*> history) {
		return history->peer->asChannel();
	}) | ranges::view::filter([&](not_null<ChannelData*> channel) {
		return !base::contains(channels, channel);
	}) | ranges::to_vector;

	const auto add = ranges::view::all(
		channels
	) | ranges::view::filter([&](not_null<ChannelData*> channel) {
		return ranges::find(
			_channels,
			channel.get(),
			[](auto history) { return history->peer->asChannel(); }
		) != end(_channels);
	}) | ranges::to_vector;

	for (const auto channel : remove) {
		channel->clearFeed();
	}
	for (const auto channel : add) {
		channel->setFeed(this);
	}

	_channels.clear();
	for (const auto channel : channels) {
		Assert(channel->feed() == this);

		_channels.push_back(App::history(channel));
	}
	_channelsLoaded = true;
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

void Feed::setUnreadPosition(const MessagePosition &position) {
	_unreadPosition = position;
}

MessagePosition Feed::unreadPosition() const {
	return _unreadPosition.current();
}

rpl::producer<MessagePosition> Feed::unreadPositionChanges() const {
	return _unreadPosition.changes();
}

bool Feed::toImportant() const {
	return false; // TODO feeds workmode
}

bool Feed::shouldBeInChatList() const {
	return _channels.size() > 1;
}

int Feed::chatListUnreadCount() const {
	return _unreadCount;
}

bool Feed::chatListMutedBadge() const {
	return _unreadCount <= _unreadMutedCount;
}

HistoryItem *Feed::chatsListItem() const {
	return _lastMessage;
}

const QString &Feed::chatsListName() const {
	return _name;
}

const base::flat_set<QString> &Feed::chatsListNameWords() const {
	return _nameWords;
}

const base::flat_set<QChar> &Feed::chatsListFirstLetters() const {
	return _nameFirstLetters;
}

} // namespace Data
