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

} // namespace

SavedMessages::SavedMessages(not_null<Session*> owner)
: _owner(owner)
, _chatsList(
	&owner->session(),
	FilterId(),
	owner->maxPinnedChatsLimitValue(this)) {
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
	if (_loadMoreRequestId || _chatsList.loaded()) {
		return;
	}
	_loadMoreRequestId = _owner->session().api().request(
		MTPmessages_GetSavedDialogs(
			MTP_flags(0),
			MTP_int(_offsetDate),
			MTP_int(_offsetId),
			_offsetPeer ? _offsetPeer->input : MTP_inputPeerEmpty(),
			MTP_int(kPerPage),
			MTP_long(0)) // hash
	).done([=](const MTPmessages_SavedDialogs &result) {
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
		_loadMoreRequestId = 0;
		if (!list) {
			_chatsList.setLoaded();
			return;
		}
		auto lastValid = false;
		const auto selfId = _owner->session().userPeerId();
		for (const auto &dialog : *list) {
			const auto &data = dialog.data();
			const auto peer = _owner->peer(peerFromMTP(data.vpeer()));
			const auto topId = MsgId(data.vtop_message().v);
			if (const auto item = _owner->message(selfId, topId)) {
				_offsetPeer = peer;
				_offsetDate = item->date();
				_offsetId = topId;
				lastValid = true;
				sublist(peer)->applyMaybeLast(item);
			} else {
				lastValid = false;
			}
		}
		if (!lastValid) {
			LOG(("API Error: Unknown message in the end of a slice."));
			_chatsList.setLoaded();
		} else if (result.type() == mtpc_messages_savedDialogs) {
			_chatsList.setLoaded();
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"SAVED_DIALOGS_UNSUPPORTED"_q) {
			_unsupported = true;
		}
		_chatsList.setLoaded();
		_loadMoreRequestId = 0;
	}).send();
}

void SavedMessages::loadMore(not_null<SavedSublist*> sublist) {
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
		auto list = (const QVector<MTPMessage>*)nullptr;
		result.match([](const MTPDmessages_channelMessages &) {
			LOG(("API Error: messages.channelMessages in sublist."));
		}, [](const MTPDmessages_messagesNotModified &) {
			LOG(("API Error: messages.messagesNotModified in sublist."));
		}, [&](const auto &data) {
			owner().processUsers(data.vusers());
			owner().processChats(data.vchats());
			list = &data.vmessages().v;
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
		sublist->append(std::move(items));
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
}

} // namespace Data
