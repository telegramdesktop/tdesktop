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

// #feed
//MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position) {
//	Expects(position.type() == mtpc_feedPosition);
//
//	const auto &data = position.c_feedPosition();
//	return MessagePosition(data.vdate.v, FullMsgId(
//		peerToChannel(peerFromMTP(data.vpeer)),
//		data.vid.v));
//}

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
		_parent->session().storage().invalidate(
			Storage::FeedMessagesInvalidate(_id));

		if (history->lastMessageKnown()) {
			if (const auto last = history->lastMessage()) {
				if (justUpdateLastMessage(last)) {
					updateChatListEntry();
				}
			}
		} else if (lastMessageKnown()) {
			_parent->session().api().requestDialogEntry(history);
		}
		if (unreadCountKnown()) {
			if (history->unreadCountKnown()) {
				// If history unreadCount is known that means that we've
				// already had the channel information and if it was in the
				// feed already (not yet known) it wouldn't get here.
				// That means here we get if we add a new channel to feed.
				if (const auto count = history->unreadCount()) {
					unreadCountChanged(count, history->mute() ? count : 0);
				}
			} else if (!_settingChannels) {
				_parent->session().api().requestDialogEntry(this);
			}
		}
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
		_parent->session().storage().remove(
			Storage::FeedMessagesRemoveAll(_id, channel->bareId()));

		if (lastMessageKnown()) {
			if (const auto last = lastMessage()) {
				if (last->history() == history) {
					recountLastMessage();
				}
			}
		}
		if (unreadCountKnown()) {
			if (history->unreadCountKnown()) {
				if (const auto delta = -history->unreadCount()) {
					unreadCountChanged(delta, history->mute() ? delta : 0);
				}
			} else {
				_parent->session().api().requestDialogEntry(this);
			}
		}
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
	if (justUpdateLastMessage(item)) {
		if (_lastMessage && *_lastMessage) {
			setChatsListTimeId((*_lastMessage)->date());
		}
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
	const auto ordered = ranges::view::all(
		_channels
	) | ranges::view::transform([](not_null<History*> history) {
		return history->peer->bareId();
	}) | ranges::to_vector | ranges::action::sort;
	return Api::CountHash(ordered);
}

bool Feed::channelsLoaded() const {
	return _channelsLoaded;
}

void Feed::setChannelsLoaded(bool loaded) {
	if (_channelsLoaded != loaded) {
		_channelsLoaded = loaded;
		_parent->notifyFeedUpdated(this, FeedUpdateFlag::Channels);
	}
}

void Feed::setChannels(std::vector<not_null<ChannelData*>> channels) {
	const auto remove = ranges::view::all(
		_channels
	) | ranges::view::transform([](not_null<History*> history) {
		return not_null<ChannelData*>(history->peer->asChannel());
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
		) == end(_channels);
	}) | ranges::view::transform([](ChannelData *channel) {
		return not_null<ChannelData*>(channel);
	}) | ranges::to_vector;

	changeChannelsList(add, remove);

	setChannelsLoaded(true);
}

void Feed::changeChannelsList(
		const std::vector<not_null<ChannelData*>> &add,
		const std::vector<not_null<ChannelData*>> &remove) {
	_settingChannels = true;
	const auto restore = gsl::finally([&] { _settingChannels = false; });

	for (const auto channel : remove) {
		channel->clearFeed();
	}

	// We assume the last message was correct before requesting the list.
	// So we save it and don't allow channels from the list to change it.
	// After that we restore it.
	const auto oldLastMessage = base::take(_lastMessage);
	for (const auto channel : add) {
		_lastMessage = base::none;
		channel->setFeed(this);
	}
	_lastMessage = oldLastMessage;
}

bool Feed::justUpdateLastMessage(not_null<HistoryItem*> item) {
	if (!_lastMessage) {
		return false;
	} else if (*_lastMessage
		&& item->position() <= (*_lastMessage)->position()) {
		return false;
	}
	_lastMessage = item;
	return true;
}

void Feed::messageRemoved(not_null<HistoryItem*> item) {
	if (lastMessage() == item) {
		recountLastMessage();
	}
}

void Feed::historyCleared(not_null<History*> history) {
	if (const auto last = lastMessage()) {
		if (last->history() == history) {
			messageRemoved(last);
		}
	}
}

void Feed::recountLastMessage() {
	_lastMessage = base::none;
	for (const auto history : _channels) {
		if (!history->lastMessageKnown()) {
			_parent->session().api().requestDialogEntry(this);
			return;
		}
	}
	setLastMessageFromChannels();
}

void Feed::setLastMessageFromChannels() {
	_lastMessage = nullptr;
	for (const auto history : _channels) {
		if (const auto last = history->lastMessage()) {
			justUpdateLastMessage(last);
		}
	}
	updateChatsListDate();
}

void Feed::updateChatsListDate() {
	if (_lastMessage && *_lastMessage) {
		setChatsListTimeId((*_lastMessage)->date());
	}
}

HistoryItem *Feed::lastMessage() const {
	return _lastMessage ? *_lastMessage : nullptr;
}

bool Feed::lastMessageKnown() const {
	return !!_lastMessage;
}

int Feed::unreadCount() const {
	return _unreadCount ? *_unreadCount : 0;
}

rpl::producer<int> Feed::unreadCountValue() const {
	return rpl::single(
		unreadCount()
	) | rpl::then(_unreadCountChanges.events());
}

bool Feed::unreadCountKnown() const {
	return !!_unreadCount;
}
// #feed
//void Feed::applyDialog(const MTPDdialogFeed &data) {
//	const auto addChannel = [&](ChannelId channelId) {
//		if (const auto channel = App::channelLoaded(channelId)) {
//			channel->setFeed(this);
//		}
//	};
//	for (const auto &channelId : data.vfeed_other_channels.v) {
//		addChannel(channelId.v);
//	}
//
//	_lastMessage = nullptr;
//	if (const auto peerId = peerFromMTP(data.vpeer)) {
//		if (const auto channelId = peerToChannel(peerId)) {
//			addChannel(channelId);
//			const auto fullId = FullMsgId(channelId, data.vtop_message.v);
//			if (const auto item = App::histItemById(fullId)) {
//				justUpdateLastMessage(item);
//			}
//		}
//	}
//	updateChatsListDate();
//
//	setUnreadCounts(
//		data.vunread_count.v,
//		data.vunread_muted_count.v);
//	if (data.has_read_max_position()) {
//		setUnreadPosition(FeedPositionFromMTP(data.vread_max_position));
//	}
//}

void Feed::changedInChatListHook(Dialogs::Mode list, bool added) {
	if (list == Dialogs::Mode::All && unreadCount()) {
		const auto mutedCount = _unreadMutedCount;
		const auto nonMutedCount = unreadCount() - mutedCount;
		const auto mutedDelta = added ? mutedCount : -mutedCount;
		const auto nonMutedDelta = added ? nonMutedCount : -nonMutedCount;
		App::histories().unreadIncrement(nonMutedDelta, false);
		App::histories().unreadIncrement(mutedDelta, true);
	}
}

void Feed::setUnreadCounts(int unreadNonMutedCount, int unreadMutedCount) {
	if (unreadCountKnown()
		&& (*_unreadCount == unreadNonMutedCount + unreadMutedCount)
		&& (_unreadMutedCount == unreadMutedCount)) {
		return;
	}
	const auto unreadNonMutedCountDelta = _unreadCount | [&](int count) {
		return unreadNonMutedCount - (count - _unreadMutedCount);
	};
	const auto unreadMutedCountDelta = _unreadCount | [&](int count) {
		return unreadMutedCount - _unreadMutedCount;
	};
	_unreadCount = unreadNonMutedCount + unreadMutedCount;
	_unreadMutedCount = unreadMutedCount;

	_unreadCountChanges.fire(unreadCount());
	updateChatListEntry();

	if (inChatList(Dialogs::Mode::All)) {
		App::histories().unreadIncrement(
			unreadNonMutedCountDelta ? *unreadNonMutedCountDelta : unreadNonMutedCount,
			false);
		App::histories().unreadIncrement(
			unreadMutedCountDelta ? *unreadMutedCountDelta : unreadMutedCount,
			true);
	}
}

void Feed::setUnreadPosition(const MessagePosition &position) {
	if (_unreadPosition.current() < position) {
		_unreadPosition = position;
	}
}

void Feed::unreadCountChanged(
		int unreadCountDelta,
		int mutedCountDelta) {
	if (!unreadCountKnown()) {
		return;
	}
	accumulate_max(unreadCountDelta, -*_unreadCount);
	*_unreadCount += unreadCountDelta;

	mutedCountDelta = snap(
		mutedCountDelta,
		-_unreadMutedCount,
		*_unreadCount - _unreadMutedCount);
	_unreadMutedCount += mutedCountDelta;

	_unreadCountChanges.fire(unreadCount());
	updateChatListEntry();

	if (inChatList(Dialogs::Mode::All)) {
		App::histories().unreadIncrement(
			unreadCountDelta,
			false);
		App::histories().unreadMuteChanged(mutedCountDelta, true);
	}
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

bool Feed::useProxyPromotion() const {
	return false;
}

bool Feed::shouldBeInChatList() const {
	return _channels.size() > 1;
}

int Feed::chatListUnreadCount() const {
	return unreadCount();
}

bool Feed::chatListUnreadMark() const {
	return false; // #feed unread mark
}

bool Feed::chatListMutedBadge() const {
	return _unreadCount ? (*_unreadCount <= _unreadMutedCount) : false;
}

HistoryItem *Feed::chatsListItem() const {
	return lastMessage();
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
