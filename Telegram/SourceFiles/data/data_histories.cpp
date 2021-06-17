/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_histories.h"

#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_scheduled_messages.h"
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
	DEBUG_LOG(("Reading: readInbox called."));
	if (history->lastServerMessageKnown()) {
		const auto last = history->lastServerMessage();
		DEBUG_LOG(("Reading: last known, reading till %1."
			).arg(last ? last->id : 0));
		readInboxTill(history, last ? last->id : 0);
		return;
	} else if (history->loadedAtBottom()) {
		if (const auto lastId = history->maxMsgId()) {
			DEBUG_LOG(("Reading: loaded at bottom, maxMsgId %1."
				).arg(lastId));
			readInboxTill(history, lastId);
			return;
		} else if (history->loadedAtTop()) {
			DEBUG_LOG(("Reading: loaded at bottom, loaded at top."));
			readInboxTill(history, 0);
			return;
		}
		DEBUG_LOG(("Reading: loaded at bottom, but requesting entry."));
	}
	requestDialogEntry(history, [=] {
		Expects(history->lastServerMessageKnown());

		const auto last = history->lastServerMessage();
		DEBUG_LOG(("Reading: got entry, reading till %1."
			).arg(last ? last->id : 0));
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

	DEBUG_LOG(("Reading: readInboxTill %1, force %2."
		).arg(tillId
		).arg(Logs::b(force)));

	const auto syncGuard = gsl::finally([&] {
		DEBUG_LOG(("Reading: in guard, unread %1."
			).arg(history->unreadCount()));
		if (history->unreadCount() > 0) {
			if (const auto last = history->lastServerMessage()) {
				DEBUG_LOG(("Reading: checking last %1 and %2."
					).arg(last->id
					).arg(tillId));
				if (last->id == tillId) {
					DEBUG_LOG(("Reading: locally marked as read."));
					history->setUnreadCount(0);
					history->updateChatListEntry();
				}
			}
		}
	});

	Core::App().notifications().clearIncomingFromHistory(history);

	const auto needsRequest = history->readInboxTillNeedsRequest(tillId);
	if (!needsRequest && !force) {
		DEBUG_LOG(("Reading: readInboxTill finish 1."));
		return;
	} else if (!history->trackUnreadMessages()) {
		DEBUG_LOG(("Reading: readInboxTill finish 2."));
		return;
	}
	const auto maybeState = lookup(history);
	if (maybeState && maybeState->sentReadTill >= tillId) {
		DEBUG_LOG(("Reading: readInboxTill finish 3 with %1."
			).arg(maybeState->sentReadTill));
		return;
	} else if (maybeState && maybeState->willReadTill >= tillId) {
		DEBUG_LOG(("Reading: readInboxTill finish 4 with %1 and force %2."
			).arg(maybeState->sentReadTill
			).arg(Logs::b(force)));
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
		DEBUG_LOG(("Reading: count didn't change so just update till %1"
			).arg(tillId));
		history->setInboxReadTill(tillId);
		return;
	}
	auto &state = maybeState ? *maybeState : _states[history];
	state.willReadTill = tillId;
	if (force || !stillUnread || !*stillUnread) {
		DEBUG_LOG(("Reading: will read till %1 with still unread %2"
			).arg(tillId
			).arg(stillUnread.value_or(-666)));
		state.willReadWhen = 0;
		sendReadRequests();
		if (!stillUnread) {
			return;
		}
	} else if (!state.willReadWhen) {
		DEBUG_LOG(("Reading: will read till %1 with postponed").arg(tillId));
		state.willReadWhen = crl::now() + kReadRequestTimeout;
		if (!_readRequestsTimer.isActive()) {
			_readRequestsTimer.callOnce(kReadRequestTimeout);
		}
	} else {
		DEBUG_LOG(("Reading: will read till %1 postponed already"
			).arg(tillId));
	}
	DEBUG_LOG(("Reading: marking now with till %1 and still %2"
		).arg(tillId
		).arg(*stillUnread));
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
	}).fail([=](const MTP::Error &error) {
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
	const auto histories = ranges::views::all(
		_dialogRequestsPending
	) | ranges::views::transform([](const auto &pair) {
		return pair.first;
	}) | ranges::views::filter([&](not_null<History*> history) {
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
	}).fail([=](const MTP::Error &error) {
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
		}).fail([=](const MTP::Error &error) {
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
		DEBUG_LOG(("Reading: send pending now with till %1 and when %2"
			).arg(state->willReadTill
			).arg(state->willReadWhen));
		if (state->willReadTill && state->willReadWhen) {
			state->willReadWhen = 0;
			sendReadRequests();
		}
	}
}

void Histories::sendReadRequests() {
	DEBUG_LOG(("Reading: send requests with count %1.").arg(_states.size()));
	if (_states.empty()) {
		return;
	}
	const auto now = crl::now();
	auto next = std::optional<crl::time>();
	for (auto &[history, state] : _states) {
		if (!state.willReadTill) {
			DEBUG_LOG(("Reading: skipping zero till."));
			continue;
		} else if (state.willReadWhen <= now) {
			DEBUG_LOG(("Reading: sending with till %1."
				).arg(state.willReadTill));
			sendReadRequest(history, state);
		} else if (!next || *next > state.willReadWhen) {
			DEBUG_LOG(("Reading: scheduling for later send."));
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
	DEBUG_LOG(("Reading: sending request now with till %1."
		).arg(tillId));
	sendRequest(history, RequestType::ReadInbox, [=](Fn<void()> finish) {
		DEBUG_LOG(("Reading: sending request invoked with till %1."
			).arg(tillId));
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
			}).fail([=](const MTP::Error &error) {
				finished();
			}).send();
		} else {
			return session().api().request(MTPmessages_ReadHistory(
				history->peer->input,
				MTP_int(tillId)
			)).done([=](const MTPmessages_AffectedMessages &result) {
				session().api().applyAffectedMessages(history->peer, result);
				finished();
			}).fail([=](const MTP::Error &error) {
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
	return ranges::any_of(state.sent, [](const auto &pair) {
		return pair.second.type != RequestType::History;
	});
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
		const auto fail = [=](const MTP::Error &error) {
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
		const auto fail = [=](const MTP::Error &error) {
			finish();
		};
		const auto chat = peer->asChat();
		const auto channel = peer->asChannel();
		if (!justClear && revoke && channel && channel->canDelete()) {
			return session().api().request(MTPchannels_DeleteChannel(
				channel->inputChannel
			)).done([=](const MTPUpdates &result) {
				session().api().applyUpdates(result);
			//}).fail([=](const MTP::Error &error) {
			//	if (error.type() == qstr("CHANNEL_TOO_LARGE")) {
			//		Ui::show(Box<InformBox>(tr::lng_cant_delete_channel(tr::now)));
			//	}
			}).send();
		} else if (channel) {
			return session().api().request(MTPchannels_DeleteHistory(
				channel->inputChannel,
				MTP_int(deleteTillId)
			)).done([=](const MTPBool &result) {
				finish();
			}).fail(fail).send();
		} else if (revoke && chat && chat->amCreator()) {
			return session().api().request(MTPmessages_DeleteChat(
				chat->inputChat
			)).done([=](const MTPBool &result) {
				finish();
			}).fail([=](const MTP::Error &error) {
				if (error.type() == "PEER_ID_INVALID") {
					// Try to join and delete,
					// while delete fails for non-joined.
					session().api().request(MTPmessages_AddChatUser(
						chat->inputChat,
						MTP_inputUserSelf(),
						MTP_int(0)
					)).done([=](const MTPUpdates &updates) {
						session().api().applyUpdates(updates);
						deleteAllMessages(
							history,
							deleteTillId,
							justClear,
							revoke);
					}).send();
				}
				finish();
			}).send();
		} else {
			using Flag = MTPmessages_DeleteHistory::Flag;
			const auto flags = Flag(0)
				| (justClear ? Flag::f_just_clear : Flag(0))
				| (revoke ? Flag::f_revoke : Flag(0));
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

void Histories::deleteMessages(const MessageIdsList &ids, bool revoke) {
	auto remove = std::vector<not_null<HistoryItem*>>();
	remove.reserve(ids.size());
	base::flat_map<not_null<History*>, QVector<MTPint>> idsByPeer;
	base::flat_map<not_null<PeerData*>, QVector<MTPint>> scheduledIdsByPeer;
	for (const auto itemId : ids) {
		if (const auto item = _owner->message(itemId)) {
			const auto history = item->history();
			if (item->isScheduled()) {
				const auto wasOnServer = !item->isSending()
					&& !item->hasFailed();
				if (wasOnServer) {
					scheduledIdsByPeer[history->peer].push_back(MTP_int(
						_owner->scheduledMessages().lookupId(item)));
				} else {
					_owner->scheduledMessages().removeSending(item);
				}
				continue;
			}
			remove.push_back(item);
			if (IsServerMsgId(item->id)) {
				idsByPeer[history].push_back(MTP_int(itemId.msg));
			}
		}
	}

	for (const auto &[history, ids] : idsByPeer) {
		history->owner().histories().deleteMessages(history, ids, revoke);
	}
	for (const auto &[peer, ids] : scheduledIdsByPeer) {
		peer->session().api().request(MTPmessages_DeleteScheduledMessages(
			peer->input,
			MTP_vector<MTPint>(ids)
		)).done([peer = peer](const MTPUpdates &result) {
			peer->session().api().applyUpdates(result);
		}).send();
	}

	for (const auto item : remove) {
		const auto history = item->history();
		const auto wasLast = (history->lastMessage() == item);
		const auto wasInChats = (history->chatListMessage() == item);
		item->destroy();

		if (wasLast || wasInChats) {
			history->requestChatListMessage();
		}
	}
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
	if (!id) {
		return;
	}
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
