/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_feed.h"

#include "data/data_session.h"
#include "data/data_channel.h"
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

Feed::Feed(not_null<Data::Session*> owner, FeedId id)
: Entry(this)
, _id(id)
, _owner(owner)
, _name(lang(lng_feed_name)) {
	indexNameParts();
}

Data::Session &Feed::owner() const {
	return *_owner;
}

AuthSession &Feed::session() const {
	return _owner->session();
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
	const auto history = owner().history(channel);
	if (!base::contains(_channels, history)) {
		const auto invisible = (_channels.size() < 2);
		_channels.push_back(history);
		session().storage().invalidate(
			Storage::FeedMessagesInvalidate(_id));

		if (history->chatListMessageKnown()) {
			if (const auto last = history->chatListMessage()) {
				if (justUpdateChatListMessage(last)) {
					updateChatListEntry();
				}
			}
		} else if (chatListMessageKnown()) {
			history->requestChatListMessage();
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
				session().api().requestDialogEntry(this);
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
		_owner->notifyFeedUpdated(this, FeedUpdateFlag::Channels);
	}
}

void Feed::unregisterOne(not_null<ChannelData*> channel) {
	const auto history = owner().history(channel);
	const auto i = ranges::remove(_channels, history);
	if (i != end(_channels)) {
		const auto visible = (_channels.size() > 1);
		_channels.erase(i, end(_channels));
		session().storage().remove(
			Storage::FeedMessagesRemoveAll(_id, channel->bareId()));

		if (chatListMessageKnown()) {
			if (const auto last = chatListMessage()) {
				if (last->history() == history) {
					recountChatListMessage();
				}
			}
		}
		if (unreadCountKnown()) {
			if (history->unreadCountKnown()) {
				if (const auto delta = -history->unreadCount()) {
					unreadCountChanged(delta, history->mute() ? delta : 0);
				}
			} else {
				session().api().requestDialogEntry(this);
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
		_owner->notifyFeedUpdated(this, FeedUpdateFlag::Channels);
	}
}

void Feed::updateChatListMessage(not_null<HistoryItem*> item) {
	if (justUpdateChatListMessage(item)) {
		if (_chatListMessage && *_chatListMessage) {
			setChatListTimeId((*_chatListMessage)->date());
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
		_owner->notifyFeedUpdated(this, FeedUpdateFlag::Channels);
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
	const auto oldChatListMessage = base::take(_chatListMessage);
	for (const auto channel : add) {
		_chatListMessage = std::nullopt;
		channel->setFeed(this);
	}
	_chatListMessage = oldChatListMessage;
}

bool Feed::justUpdateChatListMessage(not_null<HistoryItem*> item) {
	if (!_chatListMessage) {
		return false;
	} else if (*_chatListMessage
		&& item->position() <= (*_chatListMessage)->position()) {
		return false;
	}
	_chatListMessage = item;
	return true;
}

void Feed::messageRemoved(not_null<HistoryItem*> item) {
	if (chatListMessage() == item) {
		recountChatListMessage();
	}
}

void Feed::historyCleared(not_null<History*> history) {
	if (const auto last = chatListMessage()) {
		if (last->history() == history) {
			messageRemoved(last);
		}
	}
}

void Feed::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		session().api().requestDialogEntry(this);
	}
}

void Feed::recountChatListMessage() {
	_chatListMessage = std::nullopt;
	for (const auto history : _channels) {
		if (!history->chatListMessageKnown()) {
			requestChatListMessage();
			return;
		}
	}
	setChatListMessageFromChannels();
}

void Feed::setChatListMessageFromChannels() {
	_chatListMessage = nullptr;
	for (const auto history : _channels) {
		if (const auto last = history->chatListMessage()) {
			justUpdateChatListMessage(last);
		}
	}
	updateChatListDate();
}

void Feed::updateChatListDate() {
	if (_chatListMessage && *_chatListMessage) {
		setChatListTimeId((*_chatListMessage)->date());
	}
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
//		if (const auto channel = owner().channelLoaded(channelId)) {
//			channel->setFeed(this);
//		}
//	};
//	for (const auto &channelId : data.vfeed_other_channels.v) {
//		addChannel(channelId.v);
//	}
//
//	_chatListMessage = nullptr;
//	if (const auto peerId = peerFromMTP(data.vpeer)) {
//		if (const auto channelId = peerToChannel(peerId)) {
//			addChannel(channelId);
//			const auto fullId = FullMsgId(channelId, data.vtop_message.v);
//			if (const auto item = App::histItemById(fullId)) {
//				justUpdateChatListMessage(item);
//			}
//		}
//	}
//	updateChatListDate();
//
//	setUnreadCounts(
//		data.vunread_count.v,
//		data.vunread_muted_count.v);
//	if (data.has_read_max_position()) {
//		setUnreadPosition(FeedPositionFromMTP(data.vread_max_position));
//	}
//}

void Feed::changedInChatListHook(Dialogs::Mode list, bool added) {
	if (list != Dialogs::Mode::All) {
		return;
	}
	if (const auto count = unreadCount()) {
		const auto mutedCount = _unreadMutedCount;
		const auto nonMutedCount = count - mutedCount;
		const auto mutedDelta = added ? mutedCount : -mutedCount;
		const auto nonMutedDelta = added ? nonMutedCount : -nonMutedCount;
		Auth().data().unreadIncrement(nonMutedDelta, false);
		Auth().data().unreadIncrement(mutedDelta, true);

		const auto fullMuted = (nonMutedCount == 0);
		const auto entriesWithUnreadDelta = added ? 1 : -1;
		const auto mutedEntriesWithUnreadDelta = fullMuted
			? entriesWithUnreadDelta
			: 0;
		Auth().data().unreadEntriesChanged(
			entriesWithUnreadDelta,
			mutedEntriesWithUnreadDelta);
	}
}

template <typename PerformUpdate>
void Feed::updateUnreadCounts(PerformUpdate &&performUpdate) {
	const auto wasUnreadCount = _unreadCount  ? *_unreadCount : 0;
	const auto wasUnreadMutedCount = _unreadMutedCount;
	const auto wasFullMuted = (wasUnreadMutedCount > 0)
		&& (wasUnreadCount == wasUnreadMutedCount);

	performUpdate();
	Assert(_unreadCount.has_value());

	_unreadCountChanges.fire(unreadCount());
	updateChatListEntry();

	if (inChatList(Dialogs::Mode::All)) {
		const auto nowUnreadCount = *_unreadCount;
		const auto nowUnreadMutedCount = _unreadMutedCount;
		const auto nowFullMuted = (nowUnreadMutedCount > 0)
			&& (nowUnreadCount == nowUnreadMutedCount);

		Auth().data().unreadIncrement(
			(nowUnreadCount - nowUnreadMutedCount)
			- (wasUnreadCount - wasUnreadMutedCount),
			false);
		Auth().data().unreadIncrement(
			nowUnreadMutedCount - wasUnreadMutedCount,
			true);

		const auto entriesDelta = (nowUnreadCount && !wasUnreadCount)
			? 1
			: (wasUnreadCount && !nowUnreadCount)
			? -1
			: 0;
		const auto mutedEntriesDelta = (!wasFullMuted && nowFullMuted)
			? 1
			: (wasFullMuted && !nowFullMuted)
			? -1
			: 0;
		Auth().data().unreadEntriesChanged(
			entriesDelta,
			mutedEntriesDelta);
	}
}

void Feed::setUnreadCounts(int unreadNonMutedCount, int unreadMutedCount) {
	if (unreadCountKnown()
		&& (*_unreadCount == unreadNonMutedCount + unreadMutedCount)
		&& (_unreadMutedCount == unreadMutedCount)) {
		return;
	}
	updateUnreadCounts([&] {
		_unreadCount = unreadNonMutedCount + unreadMutedCount;
		_unreadMutedCount = unreadMutedCount;
	});
}

void Feed::setUnreadPosition(const MessagePosition &position) {
	if (_unreadPosition.current() < position) {
		_unreadPosition = position;
	}
}

void Feed::unreadCountChanged(int unreadCountDelta, int mutedCountDelta) {
	if (!unreadCountKnown()) {
		return;
	}
	updateUnreadCounts([&] {
		accumulate_max(unreadCountDelta, -*_unreadCount);
		*_unreadCount += unreadCountDelta;

		mutedCountDelta = snap(
			mutedCountDelta,
			-_unreadMutedCount,
			*_unreadCount - _unreadMutedCount);
		_unreadMutedCount += mutedCountDelta;
	});
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

HistoryItem *Feed::chatListMessage() const {
	return _chatListMessage ? *_chatListMessage : nullptr;
}

bool Feed::chatListMessageKnown() const {
	return _chatListMessage.has_value();
}

const QString &Feed::chatListName() const {
	return _name;
}

const base::flat_set<QString> &Feed::chatListNameWords() const {
	return _nameWords;
}

const base::flat_set<QChar> &Feed::chatListFirstLetters() const {
	return _nameFirstLetters;
}

} // namespace Data
