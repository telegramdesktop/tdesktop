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
#include "styles/style_dialogs.h" // st::dialogsArchiveUserpic

namespace Data {
namespace {

constexpr auto kLoadedChatsMinCount = 20;

} // namespace

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
: Entry(owner, this)
, _id(id)
, _chatsList(Dialogs::SortMode::Date)
, _importantChatsList(Dialogs::SortMode::Date)
, _name(lang(lng_archived_chats)) {
	indexNameParts();
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

void Folder::registerOne(not_null<History*> history) {
	//session().storage().invalidate( // #feed
	//	Storage::FeedMessagesInvalidate(_id));

	if (unreadCountKnown()) {
		if (history->unreadCountKnown()) {
			// If history unreadCount is known that means that we've
			// already had the channel information and if it was in the
			// feed already (not yet known) it wouldn't get here.
			// That means here we get if we add a new channel to feed.
			if (const auto count = history->unreadCount()) {
				unreadCountChanged(count, history->mute() ? count : 0);
			}
		} else {
			session().api().requestDialogEntry(this);
		}
	}
	if (_chatsList.size() == 1) {
		updateChatListSortPosition();
	}
	owner().notifyFolderUpdated(this, FolderUpdateFlag::List);
}

void Folder::unregisterOne(not_null<History*> history) {
	//session().storage().remove( // #feed
	//	Storage::FeedMessagesRemoveAll(_id, channel->bareId()));

	if (unreadCountKnown()) {
		if (history->unreadCountKnown()) {
			if (const auto delta = -history->unreadCount()) {
				unreadCountChanged(delta, history->mute() ? delta : 0);
			}
		} else {
			session().api().requestDialogEntry(this);
		}
	}
	if (_chatsList.empty()) {
		updateChatListExistence();
	}
	owner().notifyFolderUpdated(this, FolderUpdateFlag::List);
}

not_null<Dialogs::IndexedList*> Folder::chatsList(Dialogs::Mode list) {
	return (list == Dialogs::Mode::All)
		? &_chatsList
		: &_importantChatsList;
}

void Folder::loadUserpic() {
	//constexpr auto kPaintUserpicsCount = 4; // #feed
	//auto load = kPaintUserpicsCount;
	//for (const auto history : _histories) {
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
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyPeerArchiveUserpicBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(x, y, size, size, size / 3., size / 3.);
	}
	st::dialogsArchiveUserpic.paintInCenter(p, { x, y, size, size });
	//const auto small = (size - st::lineWidth) / 2; // #feed
	//const auto delta = size - small;
	//auto index = 0;
	//for (const auto history : _histories) {
	//	history->peer->paintUserpic(p, x, y, small);
	//	switch (++index) {
	//	case 1:
	//	case 3: x += delta; break;
	//	case 2: x -= delta; y += delta; break;
	//	case 4: return;
	//	}
	//}
}

bool Folder::chatsListLoaded() const {
	return _chatsListLoaded;
}

void Folder::setChatsListLoaded(bool loaded) {
	if (_chatsListLoaded != loaded) {
		_chatsListLoaded = loaded;
		owner().notifyFolderUpdated(this, FolderUpdateFlag::List);
	}
}
// // #feed
//int32 Folder::chatsHash() const {
//	const auto ordered = ranges::view::all(
//		_histories
//	) | ranges::view::transform([](not_null<History*> history) {
//		return history->peer->bareId();
//	}) | ranges::to_vector | ranges::action::sort;
//	return Api::CountHash(ordered);
//}
//
//void Folder::setChats(std::vector<not_null<PeerData*>> chats) {
//	const auto remove = ranges::view::all(
//		_histories
//	) | ranges::view::transform([](not_null<History*> history) {
//		return history->peer;
//	}) | ranges::view::filter([&](not_null<PeerData*> peer) {
//		return !base::contains(chats, peer);
//	}) | ranges::to_vector;
//
//	const auto add = ranges::view::all(
//		chats
//	) | ranges::view::filter([&](not_null<PeerData*> peer) {
//		return ranges::find(
//			_histories,
//			peer,
//			[](auto history) { return history->peer; }
//		) == end(_histories);
//	}) | ranges::view::transform([](PeerData *peer) {
//		return not_null<PeerData*>(peer);
//	}) | ranges::to_vector;
//
//	changeChatsList(add, remove);
//
//	setChatsLoaded(true);
//}
//
//void Folder::changeChatsList(
//		const std::vector<not_null<PeerData*>> &add,
//		const std::vector<not_null<PeerData*>> &remove) {
//	_settingChats = true;
//	const auto restore = gsl::finally([&] { _settingChats = false; });
//
//	for (const auto channel : remove) {
//		channel->clearFeed();
//	}
//
//	//// We assume the last message was correct before requesting the list.
//	//// So we save it and don't allow channels from the list to change it.
//	//// After that we restore it.
//	const auto oldChatListMessage = base::take(_chatListMessage);
//	for (const auto channel : add) {
//		_chatListMessage = std::nullopt;
//		channel->setFeed(this);
//	}
//	_chatListMessage = oldChatListMessage;
//}

void Folder::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		session().api().requestDialogEntry(this);
	}
}

TimeId Folder::adjustedChatListTimeId() const {
	return _chatsList.empty()
		? TimeId(0)
		: (*_chatsList.begin())->entry()->adjustedChatListTimeId();
}

int Folder::unreadCount() const {
	return _unreadCount.value_or(0);
}

rpl::producer<int> Folder::unreadCountValue() const {
	return rpl::single(
		unreadCount()
	) | rpl::then(_unreadCountChanges.events());
}

bool Folder::unreadCountKnown() const {
	return !!_unreadCount;
}

void Folder::applyDialog(const MTPDdialogFolder &data) {
	//const auto addChannel = [&](ChannelId channelId) { // #feed
	//	if (const auto channel = owner().channelLoaded(channelId)) {
	//		channel->setFeed(this);
	//	}
	//};
	//for (const auto &channelId : data.vfeed_other_channels.v) {
	//	addChannel(channelId.v);
	//}

	if (const auto peerId = peerFromMTP(data.vpeer)) {
		const auto history = owner().history(peerId);
		const auto fullId = FullMsgId(
			peerToChannel(peerId),
			data.vtop_message.v);
		history->setFolder(this, App::histItemById(fullId));
	} else {
		_chatsList.clear();
		_importantChatsList.clear();
		updateChatListExistence();
	}
	setUnreadCounts(
		data.vunread_unmuted_messages_count.v,
		data.vunread_muted_messages_count.v);
	//setUnreadMark(data.is_unread_mark());
	//setUnreadMentionsCount(data.vunread_mentions_count.v);

	//if (data.has_read_max_position()) { // #feed
	//	setUnreadPosition(FeedPositionFromMTP(data.vread_max_position));
	//}

	if (_chatsList.size() < kLoadedChatsMinCount) {
		session().api().requestFolderDialogs(_id);
	}
}

void Folder::changedInChatListHook(Dialogs::Mode list, bool added) {
	if (list != Dialogs::Mode::All) {
		return;
	}
	if (const auto count = unreadCount()) {
		const auto mutedCount = _unreadMutedCount;
		const auto nonMutedCount = count - mutedCount;
		const auto mutedDelta = added ? mutedCount : -mutedCount;
		const auto nonMutedDelta = added ? nonMutedCount : -nonMutedCount;
		owner().unreadIncrement(nonMutedDelta, false);
		owner().unreadIncrement(mutedDelta, true);

		const auto fullMuted = (nonMutedCount == 0);
		const auto entriesWithUnreadDelta = added ? 1 : -1;
		const auto mutedEntriesWithUnreadDelta = fullMuted
			? entriesWithUnreadDelta
			: 0;
		owner().unreadEntriesChanged(
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

		owner().unreadIncrement(
			(nowUnreadCount - nowUnreadMutedCount)
			- (wasUnreadCount - wasUnreadMutedCount),
			false);
		owner().unreadIncrement(
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
		owner().unreadEntriesChanged(
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
// #feed
//void Folder::setUnreadPosition(const MessagePosition &position) {
//	if (_unreadPosition.current() < position) {
//		_unreadPosition = position;
//	}
//}

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
//
//void Folder::setUnreadMark(bool unread) {
//	if (_unreadMark != unread) {
//		_unreadMark = unread;
//		if (!_unreadCount || !*_unreadCount) {
//			if (inChatList(Dialogs::Mode::All)) {
//				const auto delta = _unreadMark ? 1 : -1;
//				owner().unreadIncrement(delta, mute());
//				owner().unreadEntriesChanged(
//					delta,
//					mute() ? delta : 0);
//
//				updateChatListEntry();
//			}
//		}
//		Notify::peerUpdatedDelayed(
//			peer,
//			Notify::PeerUpdate::Flag::UnreadViewChanged);
//	}
//}
//
//bool Folder::unreadMark() const {
//	return _unreadMark;
//}
// #feed
//MessagePosition Folder::unreadPosition() const {
//	return _unreadPosition.current();
//}
//
//rpl::producer<MessagePosition> Folder::unreadPositionChanges() const {
//	return _unreadPosition.changes();
//}

bool Folder::toImportant() const {
	return !_importantChatsList.empty();
}

int Folder::fixedOnTopIndex() const {
	return kArchiveFixOnTopIndex;
}

bool Folder::shouldBeInChatList() const {
	return !_chatsList.empty();
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
	return _chatsList.empty()
		? nullptr
		: (*_chatsList.begin())->key().entry()->chatListMessage();
}

bool Folder::chatListMessageKnown() const {
	return _chatsList.empty()
		|| (*_chatsList.begin())->key().entry()->chatListMessageKnown();
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
