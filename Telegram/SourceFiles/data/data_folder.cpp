/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_folder.h"

#include "data/data_session.h"
#include "data/data_channel.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "storage/storage_facade.h"
//#include "storage/storage_feed_messages.h" // #feed
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

Folder::Folder(not_null<Data::Session*> owner, FolderId id)
: Entry(this)
, _id(id)
, _owner(owner)
, _name(lang(lng_feed_name)) {
	indexNameParts();
}

Data::Session &Folder::owner() const {
	return *_owner;
}

AuthSession &Folder::session() const {
	return _owner->session();
}

FolderId Folder::id() const {
	return _id;
}

void Folder::indexNameParts() {
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

void Folder::registerOne(not_null<PeerData*> peer) {
	const auto history = owner().history(peer);
	if (!base::contains(_chats, history)) {
		const auto invisible = empty(_chats);
		_chats.push_back(history);
		//session().storage().invalidate( // #feed
		//	Storage::FeedMessagesInvalidate(_id));

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
			} else if (!_settingChats) {
				session().api().requestDialogEntry(this);
			}
		}
		if (invisible && !empty(_chats)) {
			updateChatListExistence();
			for (const auto history : _chats) {
				history->updateChatListExistence();
			}
		} else {
			history->updateChatListExistence();
		}
		_owner->notifyFolderUpdated(this, FolderUpdateFlag::List);
	}
}

void Folder::unregisterOne(not_null<PeerData*> peer) {
	const auto history = owner().history(peer);
	const auto i = ranges::remove(_chats, history);
	if (i != end(_chats)) {
		const auto visible = !empty(_chats);
		_chats.erase(i, end(_chats));
		//session().storage().remove( // #feed
		//	Storage::FeedMessagesRemoveAll(_id, channel->bareId()));

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
		if (visible && empty(_chats)) {
			updateChatListExistence();
			for (const auto history : _chats) {
				history->updateChatListExistence();
			}
		} else {
			history->updateChatListExistence();
		}
		_owner->notifyFolderUpdated(this, FolderUpdateFlag::List);
	}
}

void Folder::updateChatListMessage(not_null<HistoryItem*> item) {
	if (justUpdateChatListMessage(item)) {
		if (_chatListMessage && *_chatListMessage) {
			setChatListTimeId((*_chatListMessage)->date());
		}
	}
}

void Folder::loadUserpic() {
	//constexpr auto kPaintUserpicsCount = 4; // #feed
	//auto load = kPaintUserpicsCount;
	//for (const auto history : _chats) {
	//	history->peer->loadUserpic();
	//	if (!--load) {
	//		break;
	//	}
	//}
}

void Folder::paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const {
	//const auto small = (size - st::lineWidth) / 2; // #feed
	//const auto delta = size - small;
	//auto index = 0;
	//for (const auto history : _chats) {
	//	history->peer->paintUserpic(p, x, y, small);
	//	switch (++index) {
	//	case 1:
	//	case 3: x += delta; break;
	//	case 2: x -= delta; y += delta; break;
	//	case 4: return;
	//	}
	//}
}

const std::vector<not_null<History*>> &Folder::chats() const {
	return _chats;
}

int32 Folder::chatsHash() const {
	const auto ordered = ranges::view::all(
		_chats
	) | ranges::view::transform([](not_null<History*> history) {
		return history->peer->bareId();
	}) | ranges::to_vector | ranges::action::sort;
	return Api::CountHash(ordered);
}

bool Folder::chatsLoaded() const {
	return _chatsLoaded;
}

void Folder::setChatsLoaded(bool loaded) {
	if (_chatsLoaded != loaded) {
		_chatsLoaded = loaded;
		_owner->notifyFolderUpdated(this, FolderUpdateFlag::List);
	}
}

void Folder::setChats(std::vector<not_null<PeerData*>> chats) {
	const auto remove = ranges::view::all(
		_chats
	) | ranges::view::transform([](not_null<History*> history) {
		return history->peer;
	}) | ranges::view::filter([&](not_null<PeerData*> peer) {
		return !base::contains(chats, peer);
	}) | ranges::to_vector;

	const auto add = ranges::view::all(
		chats
	) | ranges::view::filter([&](not_null<PeerData*> peer) {
		return ranges::find(
			_chats,
			peer,
			[](auto history) { return history->peer; }
		) == end(_chats);
	}) | ranges::view::transform([](PeerData *peer) {
		return not_null<PeerData*>(peer);
	}) | ranges::to_vector;

	changeChatsList(add, remove);

	setChatsLoaded(true);
}

void Folder::changeChatsList(
		const std::vector<not_null<PeerData*>> &add,
		const std::vector<not_null<PeerData*>> &remove) {
	_settingChats = true;
	const auto restore = gsl::finally([&] { _settingChats = false; });

	//for (const auto channel : remove) { // #TODO archive
	//	channel->clearFeed();
	//}

	//// We assume the last message was correct before requesting the list.
	//// So we save it and don't allow channels from the list to change it.
	//// After that we restore it.
	const auto oldChatListMessage = base::take(_chatListMessage);
	//for (const auto channel : add) {
	//	_chatListMessage = std::nullopt;
	//	channel->setFeed(this);
	//}
	_chatListMessage = oldChatListMessage;
}

bool Folder::justUpdateChatListMessage(not_null<HistoryItem*> item) {
	if (!_chatListMessage) {
		return false;
	} else if (*_chatListMessage
		&& item->position() <= (*_chatListMessage)->position()) {
		return false;
	}
	_chatListMessage = item;
	return true;
}

void Folder::messageRemoved(not_null<HistoryItem*> item) {
	if (chatListMessage() == item) {
		recountChatListMessage();
	}
}

void Folder::historyCleared(not_null<History*> history) {
	if (const auto last = chatListMessage()) {
		if (last->history() == history) {
			messageRemoved(last);
		}
	}
}

void Folder::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		session().api().requestDialogEntry(this);
	}
}

void Folder::recountChatListMessage() {
	_chatListMessage = std::nullopt;
	for (const auto history : _chats) {
		if (!history->chatListMessageKnown()) {
			requestChatListMessage();
			return;
		}
	}
	setChatListMessageFromChannels();
}

void Folder::setChatListMessageFromChannels() {
	_chatListMessage = nullptr;
	for (const auto history : _chats) {
		if (const auto last = history->chatListMessage()) {
			justUpdateChatListMessage(last);
		}
	}
	updateChatListDate();
}

void Folder::updateChatListDate() {
	if (_chatListMessage && *_chatListMessage) {
		setChatListTimeId((*_chatListMessage)->date());
	}
}

int Folder::unreadCount() const {
	return _unreadCount ? *_unreadCount : 0;
}

rpl::producer<int> Folder::unreadCountValue() const {
	return rpl::single(
		unreadCount()
	) | rpl::then(_unreadCountChanges.events());
}

bool Folder::unreadCountKnown() const {
	return !!_unreadCount;
}
// #feed
//void Folder::applyDialog(const MTPDdialogFeed &data) {
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

void Folder::changedInChatListHook(Dialogs::Mode list, bool added) {
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
void Folder::updateUnreadCounts(PerformUpdate &&performUpdate) {
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

void Folder::setUnreadCounts(int unreadNonMutedCount, int unreadMutedCount) {
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

void Folder::setUnreadPosition(const MessagePosition &position) {
	if (_unreadPosition.current() < position) {
		_unreadPosition = position;
	}
}

void Folder::unreadCountChanged(int unreadCountDelta, int mutedCountDelta) {
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

MessagePosition Folder::unreadPosition() const {
	return _unreadPosition.current();
}

rpl::producer<MessagePosition> Folder::unreadPositionChanges() const {
	return _unreadPosition.changes();
}

bool Folder::toImportant() const {
	return false; // TODO feeds workmode
}

bool Folder::useProxyPromotion() const {
	return false;
}

bool Folder::shouldBeInChatList() const {
	return !empty(_chats);
}

int Folder::chatListUnreadCount() const {
	return unreadCount();
}

bool Folder::chatListUnreadMark() const {
	return false; // #feed unread mark
}

bool Folder::chatListMutedBadge() const {
	return _unreadCount ? (*_unreadCount <= _unreadMutedCount) : false;
}

HistoryItem *Folder::chatListMessage() const {
	return _chatListMessage ? *_chatListMessage : nullptr;
}

bool Folder::chatListMessageKnown() const {
	return _chatListMessage.has_value();
}

const QString &Folder::chatListName() const {
	return _name;
}

const base::flat_set<QString> &Folder::chatListNameWords() const {
	return _nameWords;
}

const base::flat_set<QChar> &Folder::chatListFirstLetters() const {
	return _nameFirstLetters;
}

} // namespace Data
