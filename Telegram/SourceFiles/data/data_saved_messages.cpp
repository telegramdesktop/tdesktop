/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_messages.h"

#include "apiwrap.h"
#include "core/application.h"
#include "data/components/recent_peers.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_user.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "window/notifications_manager.h"

namespace Data {
namespace {

constexpr auto kPerPage = 50;
constexpr auto kFirstPerPage = 10;
constexpr auto kListPerPage = 100;
constexpr auto kListFirstPerPage = 20;
constexpr auto kLoadedSublistsMinCount = 20;
constexpr auto kShowSublistNamesCount = 5;
constexpr auto kStalePerRequest = 100;

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

void SavedMessages::clear() {
	for (const auto &request : base::take(_sublistRequests)) {
		if (request.second.id != _staleRequestId) {
			owner().histories().cancelRequest(request.second.id);
		}
	}
	if (const auto requestId = base::take(_staleRequestId)) {
		session().api().request(requestId).cancel();
	}

	auto &storage = session().storage();
	auto &changes = session().changes();
	if (_owningHistory) {
		for (const auto &[peer, sublist] : base::take(_sublists)) {
			storage.unload(Storage::SharedMediaUnloadThread(
				_owningHistory->peer->id,
				MsgId(),
				peer->id));
			_owningHistory->setForwardDraft(MsgId(), peer->id, {});

			const auto raw = sublist.get();
			changes.sublistRemoved(raw);
			changes.entryRemoved(raw);
		}
	}
	_owningHistory = nullptr;
}

void SavedMessages::saveActiveSubsectionThread(not_null<Thread*> thread) {
	if (const auto sublist = thread->asSublist()) {
		Assert(sublist->parent() == this);
		_activeSubsectionSublist = sublist;
	} else {
		Assert(thread == _owningHistory);
		_activeSubsectionSublist = nullptr;
	}
}

Thread *SavedMessages::activeSubsectionThread() const {
	return _activeSubsectionSublist;
}

SavedMessages::~SavedMessages() {
	clear();
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

void SavedMessages::requestSomeStale() {
	if (_staleRequestId
		|| (!_offset.id && _loadMoreRequestId)
		|| _stalePeers.empty()
		|| !_parentChat) {
		return;
	}
	const auto type = Histories::RequestType::History;
	auto peers = std::vector<not_null<PeerData*>>();
	auto peerIds = QVector<MTPInputPeer>();
	peers.reserve(std::min(int(_stalePeers.size()), kStalePerRequest));
	peerIds.reserve(std::min(int(_stalePeers.size()), kStalePerRequest));
	for (auto i = begin(_stalePeers); i != end(_stalePeers);) {
		const auto peer = *i;
		i = _stalePeers.erase(i);

		peers.push_back(peer);
		peerIds.push_back(peer->input);
		if (peerIds.size() == kStalePerRequest) {
			break;
		}
	}
	if (peerIds.empty()) {
		return;
	}
	const auto call = [=] {
		for (const auto &peer : peers) {
			finishSublistRequest(peer);
		}
	};
	auto &histories = owner().histories();
	_staleRequestId = histories.sendRequest(_owningHistory, type, [=](
			Fn<void()> finish) {
		using Flag = MTPmessages_GetSavedDialogsByID::Flag;
		return session().api().request(
			MTPmessages_GetSavedDialogsByID(
				MTP_flags(Flag::f_parent_peer),
				_parentChat->input,
				MTP_vector<MTPInputPeer>(peerIds))
		).done([=](const MTPmessages_SavedDialogs &result) {
			_staleRequestId = 0;
			applyReceivedSublists(result);
			call();
			finish();
		}).fail([=] {
			_staleRequestId = 0;
			call();
			finish();
		}).send();
	});
	for (const auto &peer : peers) {
		_sublistRequests[peer].id = _staleRequestId;
	}
}

void SavedMessages::finishSublistRequest(not_null<PeerData*> peer) {
	if (const auto request = _sublistRequests.take(peer)) {
		for (const auto &callback : request->callbacks) {
			callback();
		}
	}
}

void SavedMessages::requestSublist(
		not_null<PeerData*> peer,
		Fn<void()> done) {
	if (!_parentChat) {
		return;
	}
	auto &request = _sublistRequests[peer];
	if (done) {
		request.callbacks.push_back(std::move(done));
	}
	if (!request.id
		&& _stalePeers.emplace(peer).second
		&& (_stalePeers.size() == 1)) {
		crl::on_main(&session(), [peer = _parentChat] {
			if (const auto monoforum = peer->monoforum()) {
				monoforum->requestSomeStale();
			}
		});
	}
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

void SavedMessages::clearAllUnreadReactions() {
	for (const auto &[peer, sublist] : _sublists) {
		sublist->unreadReactions().clear();
	}
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
			MTP_int(_offset.date),
			MTP_int(_offset.id),
			_offset.peer ? _offset.peer->input : MTP_inputPeerEmpty(),
			MTP_int(_offset.id ? kListPerPage : kListFirstPerPage),
			MTP_long(0)) // hash
	).done([=](const MTPmessages_SavedDialogs &result) {
		const auto applied = applyReceivedSublists(result);
		if (applied.allLoaded || _offset == applied.offset) {
			_chatsList.setLoaded();
		} else if (_offset.date > 0 && applied.offset.date > _offset.date) {
			LOG(("API Error: Bad order in messages.savedDialogs."));
			_chatsList.setLoaded();
		} else {
			_offset = applied.offset;
		}
		_loadMoreRequestId = 0;
		_chatsListChanges.fire({});
		if (_chatsList.loaded()) {
			_chatsListLoadedEvents.fire({});
		}
		reorderLastSublists();
		requestSomeStale();
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
		_pinnedRequestId = 0;
		_pinnedLoaded = true;
		applyReceivedSublists(result, true);
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

SavedMessages::ApplyResult SavedMessages::applyReceivedSublists(
		const MTPmessages_SavedDialogs &dialogs,
		bool pinned) {
	auto list = (const QVector<MTPSavedDialog>*)nullptr;
	dialogs.match([](const MTPDmessages_savedDialogsNotModified &) {
		LOG(("API Error: messages.savedDialogsNotModified."));
	}, [&](const auto &data) {
		_owner->processUsers(data.vusers());
		_owner->processChats(data.vchats());
		_owner->processMessages(
			data.vmessages(),
			NewMessageType::Existing);
		list = &data.vdialogs().v;
	});
	if (!list) {
		return { .allLoaded = true };
	}
	auto lastValid = false;
	auto result = ApplyResult();
	const auto parentPeerId = _parentChat
		? _parentChat->id
		: _owner->session().userPeerId();
	for (const auto &dialog : *list) {
		dialog.match([&](const MTPDsavedDialog &data) {
			const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
			const auto topId = MsgId(data.vtop_message().v);
			if (const auto item = _owner->message(parentPeerId, topId)) {
				result.offset.peer = peer;
				result.offset.date = item->date();
				result.offset.id = topId;
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
				result.offset.peer = peer;
				result.offset.date = item->date();
				result.offset.id = topId;
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
		result.allLoaded = true;
	} else if (dialogs.type() == mtpc_messages_savedDialogs) {
		result.allLoaded = true;
	}
	if (!_stalePeers.empty()) {
		requestSomeStale();
	}
	return result;
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
	Core::App().notifications().clearFromSublist(raw);
	owner().removeChatListEntry(raw);

	if (ranges::contains(_lastSublists, not_null(raw))) {
		reorderLastSublists();
	}
	if (_activeSubsectionSublist == raw) {
		_activeSubsectionSublist = nullptr;
	}

	_sublistDestroyed.fire(raw);
	_owner->session().recentPeers().chatOpenDestroyed(raw);
	session().changes().sublistUpdated(
		raw,
		Data::SublistUpdate::Flag::Destroyed);
	session().changes().entryUpdated(
		raw,
		Data::EntryUpdate::Flag::Destroyed);
	_sublists.erase(i);

	const auto history = owningHistory();
	history->destroyMessagesBySublist(sublistPeer);
	session().storage().unload(Storage::SharedMediaUnloadThread(
		_owningHistory->peer->id,
		MsgId(),
		sublistPeer->id));
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

void SavedMessages::markUnreadCountsUnknown(MsgId readTillId) {
	for (const auto &[peer, sublist] : _sublists) {
		if (sublist->unreadCountCurrent() > 0) {
			sublist->setInboxReadTill(readTillId, std::nullopt);
		}
	}
}

void SavedMessages::updateUnreadCounts(
		MsgId readTillId,
		const base::flat_map<not_null<SavedSublist*>, int> &counts) {
	for (const auto &[peer, sublist] : _sublists) {
		const auto raw = sublist.get();
		const auto i = counts.find(raw);
		const auto count = (i != end(counts)) ? i->second : 0;
		if (raw->unreadCountCurrent() != count) {
			raw->setInboxReadTill(readTillId, count);
		}
	}
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
