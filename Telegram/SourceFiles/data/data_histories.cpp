/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_histories.h"

#include "data/data_session.h"
#include "data/data_channel.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kReadRequestTimeout = 3 * crl::time(1000);
constexpr auto kReadRequestSent = std::numeric_limits<crl::time>::max();

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
	session().api().requestDialogEntry(history, [=] {
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
	if (!history->readInboxTillNeedsRequest(tillId) && !force) {
		return;
	} else if (!force) {
		const auto maybeState = lookup(history);
		if (maybeState && maybeState->readTill >= tillId) {
			return;
		}
	}
	const auto stillUnread = history->countStillUnreadLocal(tillId);
	if (!force
		&& stillUnread
		&& history->unreadCountKnown()
		&& *stillUnread == history->unreadCount()) {
		history->setInboxReadTill(tillId);
		return;
	}
	auto &state = _states[history];
	const auto wasReadTill = state.readTill;
	state.readTill = tillId;
	if (force || !stillUnread || !*stillUnread) {
		state.readWhen = 0;
		sendReadRequests();
		if (!stillUnread) {
			return;
		}
	} else if (!wasReadTill) {
		state.readWhen = crl::now() + kReadRequestTimeout;
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

void Histories::sendPendingReadInbox(not_null<History*> history) {
	if (const auto state = lookup(history)) {
		if (state->readTill && state->readWhen) {
			state->readWhen = 0;
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
		if (state.readTill && state.readWhen <= now) {
			sendReadRequest(history, state);
		} else if (!next || *next > state.readWhen) {
			next = state.readWhen;
		}
	}
	if (next.has_value()) {
		_readRequestsTimer.callOnce(*next - now);
	} else {
		_readRequestsTimer.cancel();
	}
}

void Histories::sendReadRequest(not_null<History*> history, State &state) {
	const auto tillId = state.readTill;
	state.readWhen = kReadRequestSent;
	sendRequest(history, RequestType::ReadInbox, [=](Fn<void()> done) {
		const auto finished = [=] {
			const auto state = lookup(history);
			Assert(state != nullptr);
			Assert(state->readTill >= tillId);

			if (history->unreadCountRefreshNeeded(tillId)) {
				session().api().requestDialogEntry(history);
			}
			if (state->readWhen == kReadRequestSent) {
				state->readWhen = 0;
				if (state->readTill == tillId) {
					state->readTill = 0;
				} else {
					sendReadRequests();
				}
			}
			done();
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
			&& state.sent.empty()
			&& (state.readTill == 0);
	};
	const auto i = _states.find(history);
	if (i != end(_states) && empty(i->second)) {
		_states.erase(i);
	}
}

int Histories::sendRequest(
		not_null<History*> history,
		RequestType type,
		Fn<mtpRequestId(Fn<void()> done)> generator) {
	Expects(type != RequestType::None);

	auto &state = _states[history];
	const auto id = ++state.autoincrement;
	const auto action = chooseAction(state, type);
	if (action == Action::Send) {
		state.sent.emplace(id, SentRequest{
			generator([=] { checkPostponed(history, id); }),
			type
		});
		if (base::take(state.thenRequestEntry)) {
			session().api().requestDialogEntry(history);
		}
	} else if (action == Action::Postpone) {
		state.postponed.emplace(
			id,
			PostponedRequest{ std::move(generator), type });
	}
	return id;
}

void Histories::checkPostponed(not_null<History*> history, int requestId) {
	const auto state = lookup(history);
	Assert(state != nullptr);

	state->sent.remove(requestId);
	if (!state->postponed.empty()) {
		auto &entry = state->postponed.front();
		const auto action = chooseAction(*state, entry.second.type, true);
		if (action == Action::Send) {
			const auto id = entry.first;
			const auto postponed = std::move(entry.second);
			state->postponed.remove(id);
			state->sent.emplace(id, SentRequest{
				postponed.generator([=] { checkPostponed(history, id); }),
				postponed.type
			});
			if (base::take(state->thenRequestEntry)) {
				session().api().requestDialogEntry(history);
			}
		} else {
			Assert(action == Action::Postpone);
		}
	}
	checkEmptyState(history);
}

Histories::Action Histories::chooseAction(
		State &state,
		RequestType type,
		bool fromPostponed) const {
	switch (type) {
	case RequestType::ReadInbox:
		for (const auto &[_, sent] : state.sent) {
			if (sent.type == RequestType::ReadInbox
				|| sent.type == RequestType::DialogsEntry
				|| sent.type == RequestType::Delete) {
				if (!fromPostponed) {
					auto &postponed = state.postponed;
					for (auto i = begin(postponed); i != end(postponed);) {
						if (i->second.type == RequestType::ReadInbox) {
							i = postponed.erase(i);
						} else {
							++i;
						}
					}
				}
				return Action::Postpone;
			}
		}
		return Action::Send;

	case RequestType::DialogsEntry:
		for (const auto &[_, sent] : state.sent) {
			if (sent.type == RequestType::DialogsEntry) {
				return Action::Skip;
			}
			if (sent.type == RequestType::ReadInbox
				|| sent.type == RequestType::Delete) {
				if (!fromPostponed) {
					auto &postponed = state.postponed;
					for (const auto &[_, postponed] : state.postponed) {
						if (postponed.type == RequestType::DialogsEntry) {
							return Action::Skip;
						}
					}
				}
				return Action::Postpone;
			}
		}
		return Action::Send;

	case RequestType::History:
		for (const auto &[_, sent] : state.sent) {
			if (sent.type == RequestType::Delete) {
				return Action::Postpone;
			}
		}
		return Action::Send;

	case RequestType::Delete:
		for (const auto &[_, sent] : state.sent) {
			if (sent.type == RequestType::History
				|| sent.type == RequestType::ReadInbox) {
				return Action::Postpone;
			}
		}
		for (auto i = begin(state.sent); i != end(state.sent);) {
			if (i->second.type == RequestType::DialogsEntry) {
				session().api().request(i->second.id).cancel();
				i = state.sent.erase(i);
				state.thenRequestEntry = true;
			}
		}
		return Action::Send;
	}
	Unexpected("Request type in Histories::chooseAction.");
}

Histories::State *Histories::lookup(not_null<History*> history) {
	const auto i = _states.find(history);
	return (i != end(_states)) ? &i->second : nullptr;
}

} // namespace Data
