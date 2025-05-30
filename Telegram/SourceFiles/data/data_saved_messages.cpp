/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_messages.h"

#include "apiwrap.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"

namespace Data {
namespace {

constexpr auto kPerPage = 50;
constexpr auto kFirstPerPage = 10;
constexpr auto kListPerPage = 100;
constexpr auto kListFirstPerPage = 20;
constexpr auto kLoadedSublistsMinCount = 20;
constexpr auto kShowSublistNamesCount = 5;

} // namespace

SavedMessages::SavedMessages(
	not_null<Session*> owner,
	ChannelData *parentChat)
: _owner(owner)
, _parentChat(parentChat)
, _owningHistory(parentChat ? owner->history(parentChat).get() : nullptr)
, _chatsList(
	&_owner->session(),
	FilterId(),
	_owner->maxPinnedChatsLimitValue(this))
, _loadMore([=] { sendLoadMoreRequests(); }) {
	// We don't assign _owningHistory for my Saved Messages here,
	// because the data structures are not ready yet.
	if (_owningHistory && _owningHistory->inChatList()) {
		preloadSublists();
	}
}

SavedMessages::~SavedMessages() {
	auto &changes = session().changes();
	if (_owningHistory) {
		for (const auto &[peer, sublist] : _sublists) {
			_owningHistory->setForwardDraft(MsgId(), peer->id, {});

			const auto raw = sublist.get();
			changes.entryRemoved(raw);
		}
	}
}

bool SavedMessages::supported() const {
	return !_unsupported;
}

void SavedMessages::markUnsupported() {
	_unsupported = true;
}

ChannelData *SavedMessages::parentChat() const {
	return _parentChat;
}

not_null<History*> SavedMessages::owningHistory() const {
	if (!_owningHistory) {
		const_cast<SavedMessages*>(this)->_owningHistory
			= _owner->history(_owner->session().user());
	}
	return _owningHistory;
}

Session &SavedMessages::owner() const {
	return *_owner;
}

Main::Session &SavedMessages::session() const {
	return _owner->session();
}

not_null<Dialogs::MainList*> SavedMessages::chatsList() {
	return &_chatsList;
}

not_null<SavedSublist*> SavedMessages::sublist(not_null<PeerData*> peer) {
	if (const auto loaded = sublistLoaded(peer)) {
		return loaded;
	}
	return _sublists.emplace(
		peer,
		std::make_unique<SavedSublist>(this, peer)).first->second.get();
}

SavedSublist *SavedMessages::sublistLoaded(not_null<PeerData*> peer) {
	const auto i = _sublists.find(peer);
	return (i != end(_sublists)) ? i->second.get() : nullptr;
}

rpl::producer<> SavedMessages::chatsListChanges() const {
	return _chatsListChanges.events();
}

rpl::producer<> SavedMessages::chatsListLoadedEvents() const {
	return _chatsListLoadedEvents.events();
}

void SavedMessages::preloadSublists() {
	if (parentChat()
		&& chatsList()->indexed()->size() < kLoadedSublistsMinCount) {
		loadMore();
	}
}

void SavedMessages::loadMore() {
	_loadMoreScheduled = true;
	_loadMore.call();
}

void SavedMessages::sendLoadMore() {
	if (_loadMoreRequestId || _chatsList.loaded()) {
		return;
	} else if (!_pinnedLoaded) {
		loadPinned();
	}
	using Flag = MTPmessages_GetSavedDialogs::Flag;
	_loadMoreRequestId = _owner->session().api().request(
		MTPmessages_GetSavedDialogs(
			MTP_flags(Flag::f_exclude_pinned
				| (_parentChat ? Flag::f_parent_peer : Flag(0))),
			_parentChat ? _parentChat->input : MTPInputPeer(),
			MTP_int(_offsetDate),
			MTP_int(_offsetId),
			_offsetPeer ? _offsetPeer->input : MTP_inputPeerEmpty(),
			MTP_int(_offsetId ? kListPerPage : kListFirstPerPage),
			MTP_long(0)) // hash
	).done([=](const MTPmessages_SavedDialogs &result) {
		apply(result, false);
		_chatsListChanges.fire({});
		if (_chatsList.loaded()) {
			_chatsListLoadedEvents.fire({});
		}
		reorderLastSublists();
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
			markUnsupported();
		}
		_chatsList.setLoaded();
		_loadMoreRequestId = 0;
	}).send();
}

void SavedMessages::loadPinned() {
	if (_pinnedRequestId || parentChat()) {
		return;
	}
	_pinnedRequestId = _owner->session().api().request(
		MTPmessages_GetPinnedSavedDialogs()
	).done([=](const MTPmessages_SavedDialogs &result) {
		apply(result, true);
		_chatsListChanges.fire({});
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
			markUnsupported();
		} else {
			_pinnedLoaded = true;
		}
		_pinnedRequestId = 0;
	}).send();
}

void SavedMessages::apply(
		const MTPmessages_SavedDialogs &result,
		bool pinned) {
	auto list = (const QVector<MTPSavedDialog>*)nullptr;
	result.match([](const MTPDmessages_savedDialogsNotModified &) {
		LOG(("API Error: messages.savedDialogsNotModified."));
	}, [&](const auto &data) {
		_owner->processUsers(data.vusers());
		_owner->processChats(data.vchats());
		_owner->processMessages(
			data.vmessages(),
			NewMessageType::Existing);
		list = &data.vdialogs().v;
	});
	if (pinned) {
		_pinnedRequestId = 0;
		_pinnedLoaded = true;
	} else {
		_loadMoreRequestId = 0;
	}
	if (!list) {
		if (!pinned) {
			_chatsList.setLoaded();
		}
		return;
	}
	auto lastValid = false;
	auto offsetDate = TimeId();
	auto offsetId = MsgId();
	auto offsetPeer = (PeerData*)nullptr;
	const auto parentPeerId = _parentChat
		? _parentChat->id
		: _owner->session().userPeerId();
	for (const auto &dialog : *list) {
		dialog.match([&](const MTPDsavedDialog &data) {
			const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
			const auto topId = MsgId(data.vtop_message().v);
			if (const auto item = _owner->message(parentPeerId, topId)) {
				offsetPeer = peer;
				offsetDate = item->date();
				offsetId = topId;
				lastValid = true;
				const auto entry = sublist(peer);
				const auto entryPinned = pinned || data.is_pinned();
				entry->applyMaybeLast(item);
				_owner->setPinnedFromEntryList(entry, entryPinned);
			} else {
				lastValid = false;
			}
		}, [&](const MTPDmonoForumDialog &data) {
			const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
			const auto topId = MsgId(data.vtop_message().v);
			if (const auto item = _owner->message(parentPeerId, topId)) {
				offsetPeer = peer;
				offsetDate = item->date();
				offsetId = topId;
				lastValid = true;
				sublist(peer)->applyMonoforumDialog(data, item);
			} else {
				lastValid = false;
			}
		});
	}
	if (pinned) {
	} else if (!lastValid) {
		LOG(("API Error: Unknown message in the end of a slice."));
		_chatsList.setLoaded();
	} else if (result.type() == mtpc_messages_savedDialogs) {
		_chatsList.setLoaded();
	} else if ((_offsetDate > 0 && offsetDate > _offsetDate)
		|| (offsetDate == _offsetDate
			&& offsetId == _offsetId
			&& offsetPeer == _offsetPeer)) {
		LOG(("API Error: Bad order in messages.savedDialogs."));
		_chatsList.setLoaded();
	} else {
		_offsetDate = offsetDate;
		_offsetId = offsetId;
		_offsetPeer = offsetPeer;
	}
}

void SavedMessages::sendLoadMoreRequests() {
	if (_loadMoreScheduled) {
		sendLoadMore();
	}
}

void SavedMessages::apply(const MTPDupdatePinnedSavedDialogs &update) {
	Expects(!parentChat());

	const auto list = update.vorder();
	if (!list) {
		loadPinned();
		return;
	}
	const auto &order = list->v;
	const auto notLoaded = [&](const MTPDialogPeer &peer) {
		return peer.match([&](const MTPDdialogPeer &data) {
			const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
			return !_sublists.contains(peer);
		}, [&](const MTPDdialogPeerFolder &data) {
			LOG(("API Error: "
				"updatePinnedSavedDialogs has folders."));
			return false;
		});
	};
	if (!ranges::none_of(order, notLoaded)) {
		loadPinned();
	} else {
		_chatsList.pinned()->applyList(this, order);
		_owner->notifyPinnedDialogsOrderUpdated();
	}
}

void SavedMessages::apply(const MTPDupdateSavedDialogPinned &update) {
	Expects(!parentChat());

	update.vpeer().match([&](const MTPDdialogPeer &data) {
		const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
		const auto i = _sublists.find(peer);
		if (i != end(_sublists)) {
			const auto entry = i->second.get();
			_owner->setChatPinned(entry, FilterId(), update.is_pinned());
		} else {
			loadPinned();
		}
	}, [&](const MTPDdialogPeerFolder &data) {
		DEBUG_LOG(("API Error: Folder in updateSavedDialogPinned."));
	});
}

void SavedMessages::applySublistDeleted(not_null<PeerData*> sublistPeer) {
	const auto i = _sublists.find(sublistPeer);
	if (i == end(_sublists)) {
		return;
	}
	const auto raw = i->second.get();
	//Core::App().notifications().clearFromTopic(raw); // #TODO monoforum
	owner().removeChatListEntry(raw);

	if (ranges::contains(_lastSublists, not_null(raw))) {
		reorderLastSublists();
	}

	_sublistDestroyed.fire(raw);
	session().changes().sublistUpdated(
		raw,
		Data::SublistUpdate::Flag::Destroyed);
	session().changes().entryUpdated(
		raw,
		Data::EntryUpdate::Flag::Destroyed);
	_sublists.erase(i);

	const auto history = owningHistory();
	history->destroyMessagesBySublist(sublistPeer);
	//session().storage().unload(Storage::SharedMediaUnloadThread(
	//	_history->peer->id,
	//	rootId));
	history->setForwardDraft(MsgId(), sublistPeer->id, {});
}

void SavedMessages::reorderLastSublists() {
	if (!_parentChat) {
		return;
	}

	// We want first kShowChatNamesCount histories, by last message date.
	const auto pred = [](
			not_null<SavedSublist*> a,
			not_null<SavedSublist*> b) {
		const auto aItem = a->chatListMessage();
		const auto bItem = b->chatListMessage();
		const auto aDate = aItem ? aItem->date() : TimeId(0);
		const auto bDate = bItem ? bItem->date() : TimeId(0);
		return aDate > bDate;
	};
	_lastSublists.clear();
	_lastSublists.reserve(kShowSublistNamesCount + 1);
	auto &&sublists = ranges::views::all(
		*_chatsList.indexed()
	) | ranges::views::transform([](not_null<Dialogs::Row*> row) {
		return row->sublist();
	});
	auto nonPinnedChecked = 0;
	for (const auto sublist : sublists) {
		const auto i = ranges::upper_bound(
			_lastSublists,
			not_null(sublist),
			pred);
		if (size(_lastSublists) < kShowSublistNamesCount
			|| i != end(_lastSublists)) {
			_lastSublists.insert(i, sublist);
		}
		if (size(_lastSublists) > kShowSublistNamesCount) {
			_lastSublists.pop_back();
		}
		if (!sublist->isPinnedDialog(FilterId())
			&& ++nonPinnedChecked >= kShowSublistNamesCount) {
			break;
		}
	}
	++_lastSublistsVersion;
	owningHistory()->updateChatListEntry();
}

void SavedMessages::listMessageChanged(HistoryItem *from, HistoryItem *to) {
	if (from || to) {
		reorderLastSublists();
	}
}

int SavedMessages::recentSublistsListVersion() const {
	return _lastSublistsVersion;
}

void SavedMessages::recentSublistsInvalidate(
		not_null<SavedSublist*> sublist) {
	Expects(_parentChat != nullptr);

	if (ranges::contains(_lastSublists, sublist)) {
		++_lastSublistsVersion;
		owningHistory()->updateChatListEntry();
	}
}

auto SavedMessages::recentSublists() const
-> const std::vector<not_null<SavedSublist*>> & {
	return _lastSublists;
}

rpl::producer<> SavedMessages::destroyed() const {
	if (!_parentChat) {
		return rpl::never<>();
	}
	return _parentChat->flagsValue(
	) | rpl::filter([=](const ChannelData::Flags::Change &update) {
		using Flag = ChannelData::Flag;
		return (update.diff & Flag::MonoforumAdmin)
			&& !(update.value & Flag::MonoforumAdmin);
	}) | rpl::take(1) | rpl::to_empty;
}

auto SavedMessages::sublistDestroyed() const
-> rpl::producer<not_null<SavedSublist*>> {
	return _sublistDestroyed.events();
}

rpl::lifetime &SavedMessages::lifetime() {
	return _lifetime;
}

} // namespace Data
