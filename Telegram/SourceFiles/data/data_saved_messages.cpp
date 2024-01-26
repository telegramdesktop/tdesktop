/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_messages.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kPerPage = 50;
constexpr auto kFirstPerPage = 10;
constexpr auto kListPerPage = 100;
constexpr auto kListFirstPerPage = 20;

} // namespace

SavedMessages::SavedMessages(not_null<Session*> owner)
: _owner(owner)
, _chatsList(
	&owner->session(),
	FilterId(),
	owner->maxPinnedChatsLimitValue(this))
, _loadMore([=] { sendLoadMoreRequests(); }) {
}

SavedMessages::~SavedMessages() = default;

bool SavedMessages::supported() const {
	return !_unsupported;
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
	const auto i = _sublists.find(peer);
	if (i != end(_sublists)) {
		return i->second.get();
	}
	return _sublists.emplace(
		peer,
		std::make_unique<SavedSublist>(peer)).first->second.get();
}

void SavedMessages::loadMore() {
	_loadMoreScheduled = true;
	_loadMore.call();
}

void SavedMessages::loadMore(not_null<SavedSublist*> sublist) {
	_loadMoreSublistsScheduled.emplace(sublist);
	_loadMore.call();
}

void SavedMessages::sendLoadMore() {
	if (_loadMoreRequestId || _chatsList.loaded()) {
		return;
	} else if (!_pinnedLoaded) {
		loadPinned();
	}
	_loadMoreRequestId = _owner->session().api().request(
		MTPmessages_GetSavedDialogs(
			MTP_flags(MTPmessages_GetSavedDialogs::Flag::f_exclude_pinned),
			MTP_int(_offsetDate),
			MTP_int(_offsetId),
			_offsetPeer ? _offsetPeer->input : MTP_inputPeerEmpty(),
			MTP_int(_offsetId ? kListPerPage : kListFirstPerPage),
			MTP_long(0)) // hash
	).done([=](const MTPmessages_SavedDialogs &result) {
		apply(result, false);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
			_unsupported = true;
		}
		_chatsList.setLoaded();
		_loadMoreRequestId = 0;
	}).send();
}

void SavedMessages::loadPinned() {
	if (_pinnedRequestId) {
		return;
	}
	_pinnedRequestId = _owner->session().api().request(
		MTPmessages_GetPinnedSavedDialogs()
	).done([=](const MTPmessages_SavedDialogs &result) {
		apply(result, true);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
			_unsupported = true;
		} else {
			_pinnedLoaded = true;
		}
		_pinnedRequestId = 0;
	}).send();
}

void SavedMessages::sendLoadMore(not_null<SavedSublist*> sublist) {
	if (_loadMoreRequests.contains(sublist) || sublist->isFullLoaded()) {
		return;
	}
	const auto &list = sublist->messages();
	const auto offsetId = list.empty() ? MsgId(0) : list.back()->id;
	const auto offsetDate = list.empty() ? MsgId(0) : list.back()->date();
	const auto limit = offsetId ? kPerPage : kFirstPerPage;
	const auto requestId = _owner->session().api().request(
		MTPmessages_GetSavedHistory(
			sublist->peer()->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(0), // add_offset
			MTP_int(limit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0)) // hash
	).done([=](const MTPmessages_Messages &result) {
		auto count = 0;
		auto list = (const QVector<MTPMessage>*)nullptr;
		result.match([](const MTPDmessages_channelMessages &) {
			LOG(("API Error: messages.channelMessages in sublist."));
		}, [](const MTPDmessages_messagesNotModified &) {
			LOG(("API Error: messages.messagesNotModified in sublist."));
		}, [&](const auto &data) {
			owner().processUsers(data.vusers());
			owner().processChats(data.vchats());
			list = &data.vmessages().v;
			if constexpr (MTPDmessages_messages::Is<decltype(data)>()) {
				count = int(list->size());
			} else {
				count = data.vcount().v;
			}
		});

		_loadMoreRequests.remove(sublist);
		if (!list) {
			sublist->setFullLoaded();
			return;
		}
		auto items = std::vector<not_null<HistoryItem*>>();
		items.reserve(list->size());
		for (const auto &message : *list) {
			const auto item = owner().addNewMessage(
				message,
				{},
				NewMessageType::Existing);
			if (item) {
				items.push_back(item);
			}
		}
		sublist->append(std::move(items), count);
		if (result.type() == mtpc_messages_messages) {
			sublist->setFullLoaded();
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
			_unsupported = true;
		}
		sublist->setFullLoaded();
		_loadMoreRequests.remove(sublist);
	}).send();
	_loadMoreRequests[sublist] = requestId;
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
	const auto selfId = _owner->session().userPeerId();
	for (const auto &dialog : *list) {
		const auto &data = dialog.data();
		const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
		const auto topId = MsgId(data.vtop_message().v);
		if (const auto item = _owner->message(selfId, topId)) {
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
	for (const auto sublist : base::take(_loadMoreSublistsScheduled)) {
		sendLoadMore(sublist);
	}
}

void SavedMessages::apply(const MTPDupdatePinnedSavedDialogs &update) {
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

} // namespace Data
