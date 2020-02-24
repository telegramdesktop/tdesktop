/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_histories.h"

#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "core/application.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kReadRequestTimeout = 3 * crl::time(1000);

} // namespace

Histories::Histories(not_null<Session*> owner)
: _owner(owner)
, _readRequestsTimer([=] { sendReadRequests(); }) {
}

Session &Histories::owner() const {
	return *_owner;
}

Main::Session &Histories::session() const {
	return _owner->session();
}

History *Histories::find(PeerId peerId) {
	const auto i = peerId ? _map.find(peerId) : end(_map);
	return (i != end(_map)) ? i->second.get() : nullptr;
}

not_null<History*> Histories::findOrCreate(PeerId peerId) {
	Expects(peerId != 0);

	if (const auto result = find(peerId)) {
		return result;
	}
	const auto [i, ok] = _map.emplace(
		peerId,
		std::make_unique<History>(&owner(), peerId));
	return i->second.get();
}

void Histories::unloadAll() {
	for (const auto &[peerId, history] : _map) {
		history->clear(History::ClearType::Unload);
	}
}

void Histories::clearAll() {
	_map.clear();
}

void Histories::readInbox(not_null<History*> history) {
	if (history->lastServerMessageKnown()) {
		const auto last = history->lastServerMessage();
		readInboxTill(history, last ? last->id : 0);
		return;
	} else if (history->loadedAtBottom()) {
		if (const auto lastId = history->maxMsgId()) {
			readInboxTill(history, lastId);
			return;
		} else if (history->loadedAtTop()) {
			readInboxTill(history, 0);
			return;
		}
	}
	requestDialogEntry(history, [=] {
		Expects(history->lastServerMessageKnown());

		const auto last = history->lastServerMessage();
		readInboxTill(history, last ? last->id : 0);
	});
}

void Histories::readInboxTill(not_null<HistoryItem*> item) {
	const auto history = item->history();
	if (!IsServerMsgId(item->id)) {
		readClientSideMessage(item);
		auto view = item->mainView();
		if (!view) {
			return;
		}
		auto block = view->block();
		auto blockIndex = block->indexInHistory();
		auto itemIndex = view->indexInBlock();
		while (blockIndex > 0 || itemIndex > 0) {
			if (itemIndex > 0) {
				view = block->messages[--itemIndex].get();
			} else {
				while (blockIndex > 0) {
					block = history->blocks[--blockIndex].get();
					itemIndex = block->messages.size();
					if (itemIndex > 0) {
						view = block->messages[--itemIndex].get();
						break;
					}
				}
			}
			item = view->data();
			if (IsServerMsgId(item->id)) {
				break;
			}
		}
		if (!IsServerMsgId(item->id)) {
			LOG(("App Error: "
				"Can't read history till unknown local message."));
			return;
		}
	}
	readInboxTill(history, item->id);
}

void Histories::readInboxTill(not_null<History*> history, MsgId tillId) {
	readInboxTill(history, tillId, false);
}

void Histories::readInboxTill(
		not_null<History*> history,
		MsgId tillId,
		bool force) {
	Expects(IsServerMsgId(tillId) || (!tillId && !force));

	const auto syncGuard = gsl::finally([&] {
		if (history->unreadCount() > 0) {
			if (const auto last = history->lastServerMessage()) {
				if (last->id == tillId) {
					history->setUnreadCount(0);
					history->updateChatListEntry();
				}
			}
		}
	});

	history->session().notifications().clearIncomingFromHistory(history);

	const auto needsRequest = history->readInboxTillNeedsRequest(tillId);
	if (!needsRequest && !force) {
		return;
	} else if (!history->trackUnreadMessages()) {
		return;
	}
	const auto maybeState = lookup(history);
	if (maybeState && maybeState->sentReadTill >= tillId) {
		return;
	} else if (maybeState && maybeState->willReadTill >= tillId) {
		if (force) {
			sendPendingReadInbox(history);
		}
		return;
	} else if (!needsRequest
		&& (!maybeState || !maybeState->willReadTill)) {
		return;
	}
	const auto stillUnread = history->countStillUnreadLocal(tillId);
	if (!force
		&& stillUnread
		&& history->unreadCountKnown()
		&& *stillUnread == history->unreadCount()) {
		history->setInboxReadTill(tillId);
		return;
	}
	auto &state = maybeState ? *maybeState : _states[history];
	state.willReadTill = tillId;
	if (force || !stillUnread || !*stillUnread) {
		state.willReadWhen = 0;
		sendReadRequests();
		if (!stillUnread) {
			return;
		}
	} else if (!state.willReadWhen) {
		state.willReadWhen = crl::now() + kReadRequestTimeout;
		if (!_readRequestsTimer.isActive()) {
			_readRequestsTimer.callOnce(kReadRequestTimeout);
		}
	}
	history->setInboxReadTill(tillId);
	history->setUnreadCount(*stillUnread);
	history->updateChatListEntry();
}

void Histories::readInboxOnNewMessage(not_null<HistoryItem*> item) {
	if (!IsServerMsgId(item->id)) {
		readClientSideMessage(item);
	} else {
		readInboxTill(item->history(), item->id, true);
	}
}

void Histories::readClientSideMessage(not_null<HistoryItem*> item) {
	if (item->out() || !item->unread()) {
		return;
	}
	const auto history = item->history();
	item->markClientSideAsRead();
	if (const auto unread = history->unreadCount()) {
		history->setUnreadCount(unread - 1);
	}
}

void Histories::requestDialogEntry(not_null<Data::Folder*> folder) {
	if (_dialogFolderRequests.contains(folder)) {
		return;
	}
	_dialogFolderRequests.emplace(folder);

	auto peers = QVector<MTPInputDialogPeer>(
		1,
		MTP_inputDialogPeerFolder(MTP_int(folder->id())));
	session().api().request(MTPmessages_GetPeerDialogs(
		MTP_vector(std::move(peers))
	)).done([=](const MTPmessages_PeerDialogs &result) {
		applyPeerDialogs(result);
		_dialogFolderRequests.remove(folder);
	}).fail([=](const RPCError &error) {
		_dialogFolderRequests.remove(folder);
	}).send();
}

void Histories::requestDialogEntry(
		not_null<History*> history,
		Fn<void()> callback) {
	const auto i = _dialogRequests.find(history);
	if (i != end(_dialogRequests)) {
		if (callback) {
			i->second.push_back(std::move(callback));
		}
		return;
	}

	const auto [j, ok] = _dialogRequestsPending.try_emplace(history);
	if (callback) {
		j->second.push_back(std::move(callback));
	}
	if (!ok) {
		return;
	}
	postponeRequestDialogEntries();
}

void Histories::postponeRequestDialogEntries() {
	if (_dialogRequestsPending.size() > 1) {
		return;
	}
	Core::App().postponeCall(crl::guard(&session(), [=] {
		sendDialogRequests();
	}));
}

void Histories::sendDialogRequests() {
	if (_dialogRequestsPending.empty()) {
		return;
	}
	const auto histories = ranges::view::all(
		_dialogRequestsPending
	) | ranges::view::transform([](const auto &pair) {
		return pair.first;
	}) | ranges::view::filter([&](not_null<History*> history) {
		const auto state = lookup(history);
		if (!state) {
			return true;
		} else if (!postponeEntryRequest(*state)) {
			return true;
		}
		state->postponedRequestEntry = true;
		return false;
	}) | ranges::to_vector;

	auto peers = QVector<MTPInputDialogPeer>();
	const auto dialogPeer = [](not_null<History*> history) {
		return MTP_inputDialogPeer(history->peer->input);
	};
	ranges::transform(
		histories,
		ranges::back_inserter(peers),
		dialogPeer);
	for (auto &[history, callbacks] : base::take(_dialogRequestsPending)) {
		_dialogRequests.emplace(history, std::move(callbacks));
	}

	const auto finalize = [=] {
		for (const auto history : histories) {
			const auto state = lookup(history);
			if (!state || !state->postponedRequestEntry) {
				dialogEntryApplied(history);
				history->updateChatListExistence();
			}
		}
	};
	session().api().request(MTPmessages_GetPeerDialogs(
		MTP_vector(std::move(peers))
	)).done([=](const MTPmessages_PeerDialogs &result) {
		applyPeerDialogs(result);
		finalize();
	}).fail([=](const RPCError &error) {
		finalize();
	}).send();
}

void Histories::dialogEntryApplied(not_null<History*> history) {
	const auto state = lookup(history);
	if (state && state->postponedRequestEntry) {
		return;
	}
	history->dialogEntryApplied();
	if (const auto callbacks = _dialogRequestsPending.take(history)) {
		for (const auto &callback : *callbacks) {
			callback();
		}
	}
	if (const auto callbacks = _dialogRequests.take(history)) {
		for (const auto &callback : *callbacks) {
			callback();
		}
	}
	if (state && state->sentReadTill && state->sentReadDone) {
		history->setInboxReadTill(base::take(state->sentReadTill));
		checkEmptyState(history);
	}
}

void Histories::applyPeerDialogs(const MTPmessages_PeerDialogs &dialogs) {
	Expects(dialogs.type() == mtpc_messages_peerDialogs);

	const auto &data = dialogs.c_messages_peerDialogs();
	_owner->processUsers(data.vusers());
	_owner->processChats(data.vchats());
	_owner->processMessages(data.vmessages(), NewMessageType::Last);
	for (const auto &dialog : data.vdialogs().v) {
		dialog.match([&](const MTPDdialog &data) {
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				_owner->history(peerId)->applyDialog(nullptr, data);
			}
		}, [&](const MTPDdialogFolder &data) {
			const auto folder = _owner->processFolder(data.vfolder());
			folder->applyDialog(data);
		});
	}
	_owner->sendHistoryChangeNotifications();
}

void Histories::changeDialogUnreadMark(
		not_null<History*> history,
		bool unread) {
	history->setUnreadMark(unread);

	using Flag = MTPmessages_MarkDialogUnread::Flag;
	session().api().request(MTPmessages_MarkDialogUnread(
		MTP_flags(unread ? Flag::f_unread : Flag(0)),
		MTP_inputDialogPeer(history->peer->input)
	)).send();
}

void Histories::requestFakeChatListMessage(
		not_null<History*> history) {
	if (_fakeChatListRequests.contains(history)) {
		return;
	}

	_fakeChatListRequests.emplace(history);
	sendRequest(history, RequestType::History, [=](Fn<void()> finish) {
		return session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(0),  // offset_id
			MTP_int(0),  // offset_date
			MTP_int(0),  // add_offset
			MTP_int(2),  // limit
			MTP_int(0),  // max_id
			MTP_int(0),  // min_id
			MTP_int(0)
		)).done([=](const MTPmessages_Messages &result) {
			_fakeChatListRequests.erase(history);
			history->setFakeChatListMessageFrom(result);
			finish();
		}).fail([=](const RPCError &error) {
			_fakeChatListRequests.erase(history);
			history->setFakeChatListMessageFrom(MTP_messages_messages(
				MTP_vector<MTPMessage>(0),
				MTP_vector<MTPChat>(0),
				MTP_vector<MTPUser>(0)));
			finish();
		}).send();
	});
}

void Histories::sendPendingReadInbox(not_null<History*> history) {
	if (const auto state = lookup(history)) {
		if (state->willReadTill && state->willReadWhen) {
			state->willReadWhen = 0;
			sendReadRequests();
		}
	}
}

void Histories::sendReadRequests() {
	if (_states.empty()) {
		return;
	}
	const auto now = crl::now();
	auto next = std::optional<crl::time>();
	for (auto &[history, state] : _states) {
		if (!state.willReadTill) {
			continue;
		} else if (state.willReadWhen <= now) {
			sendReadRequest(history, state);
		} else if (!next || *next > state.willReadWhen) {
			next = state.willReadWhen;
		}
	}
	if (next.has_value()) {
		_readRequestsTimer.callOnce(*next - now);
	} else {
		_readRequestsTimer.cancel();
	}
}

void Histories::sendReadRequest(not_null<History*> history, State &state) {
	Expects(state.willReadTill > state.sentReadTill);

	const auto tillId = state.sentReadTill = base::take(state.willReadTill);
	state.willReadWhen = 0;
	state.sentReadDone = false;
	sendRequest(history, RequestType::ReadInbox, [=](Fn<void()> finish) {
		const auto finished = [=] {
			const auto state = lookup(history);
			Assert(state != nullptr);

			if (state->sentReadTill == tillId) {
				state->sentReadDone = true;
				if (history->unreadCountRefreshNeeded(tillId)) {
					requestDialogEntry(history);
				} else {
					state->sentReadTill = 0;
				}
			} else {
				Assert(!state->sentReadTill || state->sentReadTill > tillId);
			}
			sendReadRequests();
			finish();
		};
		if (const auto channel = history->peer->asChannel()) {
			return session().api().request(MTPchannels_ReadHistory(
				channel->inputChannel,
				MTP_int(tillId)
			)).done([=](const MTPBool &result) {
				finished();
			}).fail([=](const RPCError &error) {
				finished();
			}).send();
		} else {
			return session().api().request(MTPmessages_ReadHistory(
				history->peer->input,
				MTP_int(tillId)
			)).done([=](const MTPmessages_AffectedMessages &result) {
				session().api().applyAffectedMessages(history->peer, result);
				finished();
			}).fail([=](const RPCError &error) {
				finished();
			}).send();
		}
	});
}

void Histories::checkEmptyState(not_null<History*> history) {
	const auto empty = [](const State &state) {
		return state.postponed.empty()
			&& !state.postponedRequestEntry
			&& state.sent.empty()
			&& (state.willReadTill == 0)
			&& (state.sentReadTill == 0);
	};
	const auto i = _states.find(history);
	if (i != end(_states) && empty(i->second)) {
		_states.erase(i);
	}
}

bool Histories::postponeHistoryRequest(const State &state) const {
	const auto proj = [](const auto &pair) {
		return pair.second.type;
	};
	const auto i = ranges::find(state.sent, RequestType::Delete, proj);
	return (i != end(state.sent));
}

bool Histories::postponeEntryRequest(const State &state) const {
	const auto i = ranges::find_if(state.sent, [](const auto &pair) {
		return pair.second.type != RequestType::History;
	});
	return (i != end(state.sent));
}

void Histories::deleteMessages(
		not_null<History*> history,
		const QVector<MTPint> &ids,
		bool revoke) {
	sendRequest(history, RequestType::Delete, [=](Fn<void()> finish) {
		const auto done = [=](const MTPmessages_AffectedMessages &result) {
			session().api().applyAffectedMessages(history->peer, result);
			finish();
			history->requestChatListMessage();
		};
		const auto fail = [=](const RPCError &error) {
			finish();
		};
		if (const auto channel = history->peer->asChannel()) {
			return session().api().request(MTPchannels_DeleteMessages(
				channel->inputChannel,
				MTP_vector<MTPint>(ids)
			)).done(done).fail(fail).send();
		} else {
			using Flag = MTPmessages_DeleteMessages::Flag;
			return session().api().request(MTPmessages_DeleteMessages(
				MTP_flags(revoke ? Flag::f_revoke : Flag(0)),
				MTP_vector<MTPint>(ids)
			)).done(done).fail(fail).send();
		}
	});
}

void Histories::deleteAllMessages(
		not_null<History*> history,
		MsgId deleteTillId,
		bool justClear,
		bool revoke) {
	sendRequest(history, RequestType::Delete, [=](Fn<void()> finish) {
		const auto peer = history->peer;
		const auto fail = [=](const RPCError &error) {
			finish();
		};
		if (const auto channel = peer->asChannel()) {
			return session().api().request(MTPchannels_DeleteHistory(
				channel->inputChannel,
				MTP_int(deleteTillId)
			)).done([=](const MTPBool &result) {
				finish();
			}).fail(fail).send();
		} else {
			using Flag = MTPmessages_DeleteHistory::Flag;
			const auto flags = Flag(0)
				| (justClear ? Flag::f_just_clear : Flag(0))
				| ((peer->isUser() && revoke) ? Flag::f_revoke : Flag(0));
			return session().api().request(MTPmessages_DeleteHistory(
				MTP_flags(flags),
				peer->input,
				MTP_int(0)
			)).done([=](const MTPmessages_AffectedHistory &result) {
				const auto offset = session().api().applyAffectedHistory(
					peer,
					result);
				if (offset > 0) {
					deleteAllMessages(
						history,
						deleteTillId,
						justClear,
						revoke);
				}
				finish();
			}).fail(fail).send();
		}
	});
}

int Histories::sendRequest(
		not_null<History*> history,
		RequestType type,
		Fn<mtpRequestId(Fn<void()> finish)> generator) {
	Expects(type != RequestType::None);

	auto &state = _states[history];
	const auto id = ++_requestAutoincrement;
	_historyByRequest.emplace(id, history);
	if (type == RequestType::History && postponeHistoryRequest(state)) {
		state.postponed.emplace(
			id,
			PostponedHistoryRequest{ std::move(generator) });
		return id;
	}
	const auto requestId = generator([=] { checkPostponed(history, id); });
	state.sent.emplace(id, SentRequest{
		std::move(generator),
		requestId,
		type
	});
	if (!state.postponedRequestEntry
		&& postponeEntryRequest(state)
		&& _dialogRequests.contains(history)) {
		state.postponedRequestEntry = true;
	}
	if (postponeHistoryRequest(state)) {
		const auto resendHistoryRequest = [&](auto &pair) {
			auto &[id, sent] = pair;
			if (sent.type != RequestType::History) {
				return false;
			}
			state.postponed.emplace(
				id,
				PostponedHistoryRequest{ std::move(sent.generator) });
			session().api().request(sent.id).cancel();
			return true;
		};
		state.sent.erase(
			ranges::remove_if(state.sent, resendHistoryRequest),
			end(state.sent));
	}
	return id;
}

void Histories::checkPostponed(not_null<History*> history, int id) {
	if (const auto state = lookup(history)) {
		finishSentRequest(history, state, id);
	}
}

void Histories::cancelRequest(int id) {
	const auto history = _historyByRequest.take(id);
	if (!history) {
		return;
	}
	const auto state = lookup(*history);
	if (!state) {
		return;
	}
	state->postponed.remove(id);
	finishSentRequest(*history, state, id);
}

void Histories::finishSentRequest(
		not_null<History*> history,
		not_null<State*> state,
		int id) {
	_historyByRequest.remove(id);
	const auto i = state->sent.find(id);
	if (i != end(state->sent)) {
		session().api().request(i->second.id).cancel();
		state->sent.erase(i);
	}
	if (!state->postponed.empty() && !postponeHistoryRequest(*state)) {
		for (auto &[id, postponed] : base::take(state->postponed)) {
			const auto requestId = postponed.generator([=, id=id] {
				checkPostponed(history, id);
			});
			state->sent.emplace(id, SentRequest{
				std::move(postponed.generator),
				requestId,
				RequestType::History
			});
		}
	}
	if (state->postponedRequestEntry && !postponeEntryRequest(*state)) {
		const auto i = _dialogRequests.find(history);
		Assert(i != end(_dialogRequests));
		const auto [j, ok] = _dialogRequestsPending.emplace(
			history,
			std::move(i->second));
		Assert(ok);
		_dialogRequests.erase(i);
		state->postponedRequestEntry = false;
		postponeRequestDialogEntries();
	}
	checkEmptyState(history);
}

Histories::State *Histories::lookup(not_null<History*> history) {
	const auto i = _states.find(history);
	return (i != end(_states)) ? &i->second : nullptr;
}

} // namespace Data
