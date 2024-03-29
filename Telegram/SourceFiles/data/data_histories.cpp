/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_histories.h"

#include "api/api_text_entities.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_scheduled_messages.h"
#include "data/data_user.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_element.h"
#include "core/application.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kReadRequestTimeout = 3 * crl::time(1000);

} // namespace

MTPInputReplyTo ReplyToForMTP(
		not_null<History*> history,
		FullReplyTo replyTo) {
	const auto owner = &history->owner();
	if (replyTo.storyId) {
		if (const auto peer = owner->peerLoaded(replyTo.storyId.peer)) {
			return MTP_inputReplyToStory(
				peer->input,
				MTP_int(replyTo.storyId.story));
		}
	} else if (replyTo.messageId || replyTo.topicRootId) {
		const auto to = LookupReplyTo(history, replyTo.messageId);
		const auto replyingToTopic = replyTo.topicRootId
			? history->peer->forumTopicFor(replyTo.topicRootId)
			: nullptr;
		const auto replyingToTopicId = replyTo.topicRootId
			? (replyingToTopic
				? replyingToTopic->rootId()
				: Data::ForumTopic::kGeneralId)
			: (to ? to->topicRootId() : Data::ForumTopic::kGeneralId);
		const auto replyToTopicId = to
			? to->topicRootId()
			: replyingToTopicId;
		const auto external = replyTo.messageId
			&& (replyTo.messageId.peer != history->peer->id
				|| replyingToTopicId != replyToTopicId);
		const auto quoteEntities = Api::EntitiesToMTP(
			&history->session(),
			replyTo.quote.entities,
			Api::ConvertOption::SkipLocal);
		using Flag = MTPDinputReplyToMessage::Flag;
		return MTP_inputReplyToMessage(
			MTP_flags((replyTo.topicRootId ? Flag::f_top_msg_id : Flag())
				| (external ? Flag::f_reply_to_peer_id : Flag())
				| (replyTo.quote.text.isEmpty()
					? Flag()
					: (Flag::f_quote_text | Flag::f_quote_offset))
				| (quoteEntities.v.isEmpty()
					? Flag()
					: Flag::f_quote_entities)),
			MTP_int(replyTo.messageId ? replyTo.messageId.msg : 0),
			MTP_int(replyTo.topicRootId),
			(external
				? owner->peer(replyTo.messageId.peer)->input
				: MTPInputPeer()),
			MTP_string(replyTo.quote.text),
			quoteEntities,
			MTP_int(replyTo.quoteOffset));
	}
	return MTPInputReplyTo();
}

MTPInputMedia WebPageForMTP(
		const Data::WebPageDraft &draft,
		bool required) {
	using Flag = MTPDinputMediaWebPage::Flag;
	return MTP_inputMediaWebPage(
		MTP_flags(((false && required) ? Flag() : Flag::f_optional)
			| (draft.forceLargeMedia ? Flag::f_force_large_media : Flag())
			| (draft.forceSmallMedia ? Flag::f_force_small_media : Flag())),
		MTP_string(draft.url));
}

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
	const auto &[i, ok] = _map.emplace(
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
			).arg(last ? last->id.bare : 0));
		readInboxTill(history, last ? last->id : 0);
		return;
	} else if (history->loadedAtBottom()) {
		if (const auto lastId = history->maxMsgId()) {
			DEBUG_LOG(("Reading: loaded at bottom, maxMsgId %1."
				).arg(lastId.bare));
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
			).arg(last ? last->id.bare : 0));
		readInboxTill(history, last ? last->id : 0);
	});
}

void Histories::readInboxTill(not_null<HistoryItem*> item) {
	const auto history = item->history();
	if (!item->isRegular()) {
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
			if (item->isRegular()) {
				break;
			}
		}
		if (!item->isRegular()) {
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
		).arg(tillId.bare
		).arg(Logs::b(force)));

	const auto syncGuard = gsl::finally([&] {
		DEBUG_LOG(("Reading: in guard, unread %1."
			).arg(history->unreadCount()));
		if (history->unreadCount() > 0) {
			if (const auto last = history->lastServerMessage()) {
				DEBUG_LOG(("Reading: checking last %1 and %2."
					).arg(last->id.bare
					).arg(tillId.bare));
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
			).arg(maybeState->sentReadTill.bare));
		return;
	} else if (maybeState && maybeState->willReadTill >= tillId) {
		DEBUG_LOG(("Reading: readInboxTill finish 4 with %1 and force %2."
			).arg(maybeState->sentReadTill.bare
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
			).arg(tillId.bare));
		history->setInboxReadTill(tillId);
		return;
	}
	auto &state = maybeState ? *maybeState : _states[history];
	state.willReadTill = tillId;
	if (force || !stillUnread || !*stillUnread) {
		DEBUG_LOG(("Reading: will read till %1 with still unread %2"
			).arg(tillId.bare
			).arg(stillUnread.value_or(-666)));
		state.willReadWhen = 0;
		sendReadRequests();
		if (!stillUnread) {
			return;
		}
	} else if (!state.willReadWhen) {
		DEBUG_LOG(("Reading: will read till %1 with postponed"
			).arg(tillId.bare));
		state.willReadWhen = crl::now() + kReadRequestTimeout;
		if (!_readRequestsTimer.isActive()) {
			_readRequestsTimer.callOnce(kReadRequestTimeout);
		}
	} else {
		DEBUG_LOG(("Reading: will read till %1 postponed already"
			).arg(tillId.bare));
	}
	DEBUG_LOG(("Reading: marking now with till %1 and still %2"
		).arg(tillId.bare
		).arg(*stillUnread));
	history->setInboxReadTill(tillId);
	history->setUnreadCount(*stillUnread);
	history->updateChatListEntry();
}

void Histories::readInboxOnNewMessage(not_null<HistoryItem*> item) {
	if (!item->isRegular()) {
		readClientSideMessage(item);
	} else {
		readInboxTill(item->history(), item->id, true);
	}
}

void Histories::readClientSideMessage(not_null<HistoryItem*> item) {
	if (item->out() || !item->unread(item->history())) {
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
	}).fail([=] {
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

	const auto &[j, ok] = _dialogRequestsPending.try_emplace(history);
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
		for (const auto &history : histories) {
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
	}).fail([=] {
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
			MTP_int(0), // offset_id
			MTP_int(0), // offset_date
			MTP_int(0), // add_offset
			MTP_int(2), // limit
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_fakeChatListRequests.erase(history);
			history->setFakeChatListMessageFrom(result);
			finish();
		}).fail([=] {
			_fakeChatListRequests.erase(history);
			history->setFakeChatListMessageFrom(MTP_messages_messages(
				MTP_vector<MTPMessage>(0),
				MTP_vector<MTPChat>(0),
				MTP_vector<MTPUser>(0)));
			finish();
		}).send();
	});
}

void Histories::requestGroupAround(not_null<HistoryItem*> item) {
	const auto history = item->history();
	const auto id = item->id;
	const auto key = GroupRequestKey{ history, item->topicRootId() };
	const auto i = _chatListGroupRequests.find(key);
	if (i != end(_chatListGroupRequests)) {
		if (i->second.aroundId == id) {
			return;
		} else {
			cancelRequest(i->second.requestId);
			_chatListGroupRequests.erase(i);
		}
	}
	constexpr auto kMaxAlbumCount = 10;
	const auto requestId = sendRequest(history, RequestType::History, [=](
			Fn<void()> finish) {
		return session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(id),
			MTP_int(0), // offset_date
			MTP_int(-kMaxAlbumCount),
			MTP_int(2 * kMaxAlbumCount - 1),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_owner->processExistingMessages(
				history->peer->asChannel(),
				result);
			_chatListGroupRequests.remove(key);
			history->migrateToOrMe()->applyChatListGroup(
				history->peer->id,
				result);
			finish();
		}).fail([=] {
			_chatListGroupRequests.remove(key);
			finish();
		}).send();
	});
	_chatListGroupRequests.emplace(
		key,
		ChatListGroupRequest{ .aroundId = id, .requestId = requestId });
}

void Histories::sendPendingReadInbox(not_null<History*> history) {
	if (const auto state = lookup(history)) {
		DEBUG_LOG(("Reading: send pending now with till %1 and when %2"
			).arg(state->willReadTill.bare
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
				).arg(state.willReadTill.bare));
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
		).arg(tillId.bare));
	sendRequest(history, RequestType::ReadInbox, [=](Fn<void()> finish) {
		DEBUG_LOG(("Reading: sending request invoked with till %1."
			).arg(tillId.bare));
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
			)).done(finished).fail(finished).send();
		} else {
			return session().api().request(MTPmessages_ReadHistory(
				history->peer->input,
				MTP_int(tillId)
			)).done([=](const MTPmessages_AffectedMessages &result) {
				session().api().applyAffectedMessages(history->peer, result);
				finished();
			}).fail([=] {
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
		if (const auto channel = history->peer->asChannel()) {
			return session().api().request(MTPchannels_DeleteMessages(
				channel->inputChannel,
				MTP_vector<MTPint>(ids)
			)).done(done).fail(finish).send();
		} else {
			using Flag = MTPmessages_DeleteMessages::Flag;
			return session().api().request(MTPmessages_DeleteMessages(
				MTP_flags(revoke ? Flag::f_revoke : Flag(0)),
				MTP_vector<MTPint>(ids)
			)).done(done).fail(finish).send();
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
		const auto chat = peer->asChat();
		const auto channel = peer->asChannel();
		if (!justClear && revoke && channel && channel->canDelete()) {
			return session().api().request(MTPchannels_DeleteChannel(
				channel->inputChannel
			)).done([=](const MTPUpdates &result) {
				session().api().applyUpdates(result);
			//}).fail([=](const MTP::Error &error) {
			//	if (error.type() == u"CHANNEL_TOO_LARGE"_q) {
			//		Ui::show(Box<Ui::InformBox>(tr::lng_cant_delete_channel(tr::now)));
			//	}
			}).send();
		} else if (channel) {
			using Flag = MTPchannels_DeleteHistory::Flag;
			return session().api().request(MTPchannels_DeleteHistory(
				MTP_flags(revoke ? Flag::f_for_everyone : Flag(0)),
				channel->inputChannel,
				MTP_int(deleteTillId)
			)).done(finish).fail(finish).send();
		} else if (revoke && chat && chat->amCreator()) {
			return session().api().request(MTPmessages_DeleteChat(
				chat->inputChat
			)).done(finish).fail([=](const MTP::Error &error) {
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
				MTP_int(0),
				MTPint(), // min_date
				MTPint() // max_date
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
			}).fail(finish).send();
		}
	});
}

void Histories::deleteMessagesByDates(
		not_null<History*> history,
		QDate firstDayToDelete,
		QDate lastDayToDelete,
		bool revoke) {
	const auto firstSecondToDelete = base::unixtime::serialize(
		{ firstDayToDelete, QTime(0, 0) }
	);
	const auto lastSecondToDelete = base::unixtime::serialize(
		{ lastDayToDelete, QTime(23, 59, 59) }
	);
	deleteMessagesByDates(
		history,
		firstSecondToDelete - 1,
		lastSecondToDelete + 1,
		revoke);
}

void Histories::deleteMessagesByDates(
		not_null<History*> history,
		TimeId minDate,
		TimeId maxDate,
		bool revoke) {
	sendRequest(history, RequestType::Delete, [=](Fn<void()> finish) {
		const auto peer = history->peer;
		using Flag = MTPmessages_DeleteHistory::Flag;
		const auto flags = Flag::f_just_clear
			| Flag::f_min_date
			| Flag::f_max_date
			| (revoke ? Flag::f_revoke : Flag(0));
		return session().api().request(MTPmessages_DeleteHistory(
			MTP_flags(flags),
			peer->input,
			MTP_int(0),
			MTP_int(minDate),
			MTP_int(maxDate)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				deleteMessagesByDates(history, minDate, maxDate, revoke);
			}
			finish();
		}).fail(finish).send();
	});
	history->destroyMessagesByDates(minDate, maxDate);
}

void Histories::deleteMessages(const MessageIdsList &ids, bool revoke) {
	auto remove = std::vector<not_null<HistoryItem*>>();
	remove.reserve(ids.size());
	base::flat_map<not_null<History*>, QVector<MTPint>> idsByPeer;
	base::flat_map<not_null<PeerData*>, QVector<MTPint>> scheduledIdsByPeer;
	base::flat_map<BusinessShortcutId, QVector<MTPint>> quickIdsByShortcut;
	for (const auto &itemId : ids) {
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
			} else if (item->isBusinessShortcut()) {
				const auto wasOnServer = !item->isSending()
					&& !item->hasFailed();
				if (wasOnServer) {
					quickIdsByShortcut[item->shortcutId()].push_back(MTP_int(
						_owner->shortcutMessages().lookupId(item)));
				} else {
					_owner->shortcutMessages().removeSending(item);
				}
				continue;
			}
			remove.push_back(item);
			if (item->isRegular()) {
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
	for (const auto &[shortcutId, ids] : quickIdsByShortcut) {
		const auto api = &_owner->session().api();
		api->request(MTPmessages_DeleteQuickReplyMessages(
			MTP_int(shortcutId),
			MTP_vector<MTPint>(ids)
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result);
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

void Histories::sendCreateTopicRequest(
		not_null<History*> history,
		MsgId rootId) {
	Expects(history->peer->isChannel());

	const auto forum = history->asForum();
	Assert(forum != nullptr);
	const auto topic = forum->topicFor(rootId);
	Assert(topic != nullptr);
	const auto randomId = base::RandomValue<uint64>();
	session().data().registerMessageRandomId(
		randomId,
		{ history->peer->id, rootId });
	const auto api = &session().api();
	using Flag = MTPchannels_CreateForumTopic::Flag;
	api->request(MTPchannels_CreateForumTopic(
		MTP_flags(Flag::f_icon_color
			| (topic->iconId() ? Flag::f_icon_emoji_id : Flag(0))),
		history->peer->asChannel()->inputChannel,
		MTP_string(topic->title()),
		MTP_int(topic->colorId()),
		MTP_long(topic->iconId()),
		MTP_long(randomId),
		MTPInputPeer() // send_as
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result, randomId);
	}).fail([=](const MTP::Error &error) {
		api->sendMessageFail(error, history->peer, randomId);
	}).send();
}

bool Histories::isCreatingTopic(
		not_null<History*> history,
		MsgId rootId) const {
	const auto forum = history->asForum();
	return forum && forum->creating(rootId);
}

int Histories::sendPreparedMessage(
		not_null<History*> history,
		FullReplyTo replyTo,
		uint64 randomId,
		Fn<PreparedMessage(not_null<History*>, FullReplyTo)> message,
		Fn<void(const MTPUpdates&, const MTP::Response&)> done,
		Fn<void(const MTP::Error&, const MTP::Response&)> fail) {
	if (isCreatingTopic(history, replyTo.topicRootId)) {
		const auto id = ++_requestAutoincrement;
		const auto creatingId = FullMsgId(
			history->peer->id,
			replyTo.topicRootId);
		auto i = _creatingTopics.find(creatingId);
		if (i == end(_creatingTopics)) {
			sendCreateTopicRequest(history, replyTo.topicRootId);
			i = _creatingTopics.emplace(creatingId).first;
		}
		i->second.push_back({
			.randomId = randomId,
			.replyTo = replyTo.messageId,
			.message = std::move(message),
			.done = std::move(done),
			.fail = std::move(fail),
			.requestId = id,
		});
		_creatingTopicRequests.emplace(id);
		return id;
	}
	const auto realReplyTo = FullReplyTo{
		.messageId = convertTopicReplyToId(history, replyTo.messageId),
		.quote = replyTo.quote,
		.storyId = replyTo.storyId,
		.topicRootId = convertTopicReplyToId(history, replyTo.topicRootId),
		.quoteOffset = replyTo.quoteOffset,
	};
	return v::match(message(history, realReplyTo), [&](const auto &request) {
		const auto type = RequestType::Send;
		return sendRequest(history, type, [=](Fn<void()> finish) {
			const auto session = &_owner->session();
			const auto api = &session->api();
			history->sendRequestId = api->request(
				base::duplicate(request)
			).done([=](
					const MTPUpdates &result,
					const MTP::Response &response) {
				api->applyUpdates(result, randomId);
				done(result, response);
				finish();
			}).fail([=](
					const MTP::Error &error,
					const MTP::Response &response) {
				fail(error, response);
				finish();
			}).afterRequest(
				history->sendRequestId
			).send();
			return history->sendRequestId;
		});
	});
}

void Histories::checkTopicCreated(FullMsgId rootId, MsgId realRoot) {
	const auto i = _creatingTopics.find(rootId);
	if (i != end(_creatingTopics)) {
		auto scheduled = base::take(i->second);
		_creatingTopics.erase(i);

		_createdTopicIds.emplace(rootId, realRoot);

		const auto history = _owner->history(rootId.peer);
		if (const auto forum = history->asForum()) {
			forum->created(rootId.msg, realRoot);
		}

		for (auto &entry : scheduled) {
			_creatingTopicRequests.erase(entry.requestId);
			sendPreparedMessage(
				history,
				FullReplyTo{
					.messageId = entry.replyTo,
					.topicRootId = realRoot,
				},
				entry.randomId,
				std::move(entry.message),
				std::move(entry.done),
				std::move(entry.fail));
		}
		for (const auto &item : history->clientSideMessages()) {
			const auto replace = [&](MsgId nowId) {
				return (nowId == rootId.msg) ? realRoot : nowId;
			};
			if (item->topicRootId() == rootId.msg) {
				item->setReplyFields(
					replace(item->replyToId()),
					realRoot,
					true);
			}
		}
	}
}

FullMsgId Histories::convertTopicReplyToId(
		not_null<History*> history,
		FullMsgId replyToId) const {
	const auto id = (history->peer->id == replyToId.peer)
		? convertTopicReplyToId(history, replyToId.msg)
		: replyToId.msg;
	return { replyToId.peer, id };
}

MsgId Histories::convertTopicReplyToId(
		not_null<History*> history,
		MsgId replyToId) const {
	if (!replyToId) {
		return {};
	}
	const auto i = _createdTopicIds.find({ history->peer->id, replyToId });
	return (i != end(_createdTopicIds)) ? i->second : replyToId;
}

void Histories::checkPostponed(not_null<History*> history, int id) {
	if (const auto state = lookup(history)) {
		finishSentRequest(history, state, id);
	}
}

void Histories::cancelRequest(int id) {
	if (!id) {
		return;
	} else if (_creatingTopicRequests.contains(id)) {
		cancelDelayedByTopicRequest(id);
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

void Histories::cancelDelayedByTopicRequest(int id) {
	for (auto &[rootId, messages] : _creatingTopics) {
		messages.erase(
			ranges::remove(messages, id, &DelayedByTopicMessage::requestId),
			end(messages));
	}
	_creatingTopicRequests.remove(id);
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
		const auto &[j, ok] = _dialogRequestsPending.emplace(
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
