/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_scheduled_messages.h"

#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "api/api_hash.h"
#include "api/api_text_entities.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_message.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRequestTimeLimit = 60 * crl::time(1000);

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

[[nodiscard]] bool HasScheduledDate(not_null<HistoryItem*> item) {
	return (item->date() != ScheduledMessages::kScheduledUntilOnlineTimestamp)
		&& (item->date() > base::unixtime::now());
}

MTPMessage PrepareMessage(const MTPMessage &message, MsgId id) {
	return message.match([&](const MTPDmessageEmpty &data) {
		return MTP_messageEmpty(
			data.vflags(),
			MTP_int(id),
			data.vpeer_id() ? *data.vpeer_id() : MTPPeer());
	}, [&](const MTPDmessageService &data) {
		return MTP_messageService(
			MTP_flags(data.vflags().v
				| MTPDmessageService::Flag(
					MTPDmessage::Flag::f_from_scheduled)),
			MTP_int(id),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			data.vreply_to() ? *data.vreply_to() : MTPMessageReplyHeader(),
			data.vdate(),
			data.vaction(),
			MTP_int(data.vttl_period().value_or_empty()));
	}, [&](const MTPDmessage &data) {
		return MTP_message(
			MTP_flags(data.vflags().v | MTPDmessage::Flag::f_from_scheduled),
			MTP_int(id),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
			MTP_int(data.vvia_bot_id().value_or_empty()),
			data.vreply_to() ? *data.vreply_to() : MTPMessageReplyHeader(),
			data.vdate(),
			data.vmessage(),
			data.vmedia() ? *data.vmedia() : MTPMessageMedia(),
			data.vreply_markup() ? *data.vreply_markup() : MTPReplyMarkup(),
			(data.ventities()
				? *data.ventities()
				: MTPVector<MTPMessageEntity>()),
			MTP_int(data.vviews().value_or_empty()),
			MTP_int(data.vforwards().value_or_empty()),
			data.vreplies() ? *data.vreplies() : MTPMessageReplies(),
			MTP_int(data.vedit_date().value_or_empty()),
			MTP_bytes(data.vpost_author().value_or_empty()),
			MTP_long(data.vgrouped_id().value_or_empty()),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTP_int(data.vttl_period().value_or_empty()));
	});
}

} // namespace

ScheduledMessages::ScheduledMessages(not_null<Session*> owner)
: _session(&owner->session())
, _clearTimer([=] { clearOldRequests(); }) {
	owner->itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isScheduled();
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);
}

ScheduledMessages::~ScheduledMessages() {
	for (const auto &request : _requests) {
		_session->api().request(request.second.requestId).cancel();
	}
}

void ScheduledMessages::clearOldRequests() {
	const auto now = crl::now();
	while (true) {
		const auto i = ranges::find_if(_requests, [&](const auto &value) {
			const auto &request = value.second;
			return !request.requestId
				&& (request.lastReceived + kRequestTimeLimit <= now);
		});
		if (i == end(_requests)) {
			break;
		}
		_requests.erase(i);
	}
}

MsgId ScheduledMessages::lookupId(not_null<HistoryItem*> item) const {
	Expects(item->isScheduled());

	const auto i = _data.find(item->history());
	Assert(i != end(_data));
	const auto &list = i->second;
	const auto j = list.idByItem.find(item);
	Assert(j != end(list.idByItem));
	return j->second;
}

HistoryItem *ScheduledMessages::lookupItem(PeerId peer, MsgId msg) const {
	const auto history = _session->data().historyLoaded(peer);
	if (!history) {
		return nullptr;
	}

	const auto i = _data.find(history);
	if (i == end(_data)) {
		return nullptr;
	}

	const auto &items = i->second.items;
	const auto j = ranges::find_if(items, [&](auto &item) {
		return item->id == msg;
	});
	if (j == end(items)) {
		return nullptr;
	}
	return (*j).get();
}

HistoryItem *ScheduledMessages::lookupItem(FullMsgId itemId) const {
	return lookupItem(peerFromChannel(itemId.channel), itemId.msg);
}

int ScheduledMessages::count(not_null<History*> history) const {
	const auto i = _data.find(history);
	return (i != end(_data)) ? i->second.items.size() : 0;
}

void ScheduledMessages::sendNowSimpleMessage(
		const MTPDupdateShortSentMessage &update,
		not_null<HistoryItem*> local) {
	Expects(local->isSending());
	Expects(local->isScheduled());
	if (HasScheduledDate(local)) {
		LOG(("Error: trying to put to history a new local message, "
			"that has scheduled date."));
		return;
	}

	// When the user sends a text message scheduled until online
	// while the recipient is already online, the server sends
	// updateShortSentMessage to the client and the client calls this method.
	// Since such messages can only be sent to recipients,
	// we know for sure that a message can't have fields such as the author,
	// views count, etc.

	const auto history = local->history();
	auto action = Api::SendAction(history);
	action.replyTo = local->replyToId();
	const auto replyHeader = NewMessageReplyHeader(action);
	auto flags = NewMessageFlags(history->peer)
		| MTPDmessage::Flag::f_entities
		| MTPDmessage::Flag::f_from_id
		| (local->replyToId()
			? MTPDmessage::Flag::f_reply_to
			: MTPDmessage::Flag(0))
		| (update.vttl_period()
			? MTPDmessage::Flag::f_ttl_period
			: MTPDmessage::Flag(0));
	auto clientFlags = NewMessageClientFlags()
		| MTPDmessage_ClientFlag::f_local_history_entry;
	const auto views = 1;
	const auto forwards = 0;
	history->addNewMessage(
		MTP_message(
			MTP_flags(flags),
			update.vid(),
			peerToMTP(_session->userPeerId()),
			peerToMTP(history->peer->id),
			MTPMessageFwdHeader(),
			MTPint(),
			replyHeader,
			update.vdate(),
			MTP_string(local->originalText().text),
			MTP_messageMediaEmpty(),
			MTPReplyMarkup(),
			Api::EntitiesToMTP(
				&history->session(),
				local->originalText().entities),
			MTP_int(views),
			MTP_int(forwards),
			MTPMessageReplies(),
			MTPint(), // edit_date
			MTP_string(),
			MTPlong(),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTP_int(update.vttl_period().value_or_empty())),
		clientFlags,
		NewMessageType::Unread);

	local->destroy();
}

void ScheduledMessages::apply(const MTPDupdateNewScheduledMessage &update) {
	const auto &message = update.vmessage();
	const auto peer = PeerFromMessage(message);
	if (!peer) {
		return;
	}
	const auto history = _session->data().historyLoaded(peer);
	if (!history) {
		return;
	}
	auto &list = _data[history];
	append(history, list, message);
	sort(list);
	_updates.fire_copy(history);
}

void ScheduledMessages::checkEntitiesAndUpdate(const MTPDmessage &data) {
	// When the user sends a message with a media scheduled until online
	// while the recipient is already online, or scheduled message
	// is already due and is sent immediately, the server sends
	// updateNewMessage or updateNewChannelMessage to the client
	// and the client calls this method.

	const auto peer = peerFromMTP(data.vpeer_id());
	const auto history = _session->data().historyLoaded(peer);
	if (!history) {
		return;
	}

	const auto i = _data.find(history);
	if (i == end(_data)) {
		return;
	}

	const auto &itemMap = i->second.itemById;
	const auto j = itemMap.find(data.vid().v);
	if (j == end(itemMap)) {
		return;
	}

	const auto existing = j->second;
	if (!HasScheduledDate(existing)) {
		// Destroy a local message, that should be in history.
		existing->updateSentContent({
			qs(data.vmessage()),
			Api::EntitiesFromMTP(_session, data.ventities().value_or_empty())
		}, data.vmedia());
		existing->updateReplyMarkup(data.vreply_markup());
		existing->updateForwardedInfo(data.vfwd_from());
		_session->data().requestItemTextRefresh(existing);

		existing->destroy();
	}
}

void ScheduledMessages::apply(
		const MTPDupdateDeleteScheduledMessages &update) {
	const auto peer = peerFromMTP(update.vpeer());
	if (!peer) {
		return;
	}
	const auto history = _session->data().historyLoaded(peer);
	if (!history) {
		return;
	}
	auto i = _data.find(history);
	if (i == end(_data)) {
		return;
	}
	for (const auto &id : update.vmessages().v) {
		const auto &list = i->second;
		const auto j = list.itemById.find(id.v);
		if (j != end(list.itemById)) {
			j->second->destroy();
			i = _data.find(history);
			if (i == end(_data)) {
				break;
			}
		}
	}
	_updates.fire_copy(history);
}

void ScheduledMessages::apply(
		const MTPDupdateMessageID &update,
		not_null<HistoryItem*> local) {
	const auto id = update.vid().v;
	const auto i = _data.find(local->history());
	Assert(i != end(_data));
	auto &list = i->second;
	const auto j = list.itemById.find(id);
	if (j != end(list.itemById)) {
		local->destroy();
	} else {
		Assert(!list.itemById.contains(local->id));
		Assert(!list.idByItem.contains(local));
		local->setRealId(local->history()->nextNonHistoryEntryId());
		list.idByItem.emplace(local, id);
		list.itemById.emplace(id, local);
	}
}

void ScheduledMessages::appendSending(not_null<HistoryItem*> item) {
	Expects(item->isSending());
	Expects(item->isScheduled());

	const auto history = item->history();
	auto &list = _data[history];
	list.items.emplace_back(item);
	sort(list);
	_updates.fire_copy(history);
}

void ScheduledMessages::removeSending(not_null<HistoryItem*> item) {
	Expects(item->isSending() || item->hasFailed());
	Expects(item->isScheduled());

	item->destroy();
}

rpl::producer<> ScheduledMessages::updates(not_null<History*> history) {
	request(history);

	return _updates.events(
	) | rpl::filter([=](not_null<History*> value) {
		return (value == history);
	}) | rpl::to_empty;
}

Data::MessagesSlice ScheduledMessages::list(not_null<History*> history) {
	auto result = Data::MessagesSlice();
	const auto i = _data.find(history);
	if (i == end(_data)) {
		const auto i = _requests.find(history);
		if (i == end(_requests)) {
			return result;
		}
		result.fullCount = result.skippedAfter = result.skippedBefore = 0;
		return result;
	}
	const auto &list = i->second.items;
	result.skippedAfter = result.skippedBefore = 0;
	result.fullCount = int(list.size());
	result.ids = ranges::views::all(
		list
	) | ranges::views::transform(
		&HistoryItem::fullId
	) | ranges::to_vector;
	return result;
}

void ScheduledMessages::request(not_null<History*> history) {
	auto &request = _requests[history];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	const auto i = _data.find(history);
	const auto hash = (i != end(_data)) ? countListHash(i->second) : 0;
	request.requestId = _session->api().request(
		MTPmessages_GetScheduledHistory(
			history->peer->input,
			MTP_int(hash))
	).done([=](const MTPmessages_Messages &result) {
		parse(history, result);
	}).fail([=](const MTP::Error &error) {
		_requests.remove(history);
	}).send();
}

void ScheduledMessages::parse(
		not_null<History*> history,
		const MTPmessages_Messages &list) {
	auto &request = _requests[history];
	request.lastReceived = crl::now();
	request.requestId = 0;
	if (!_clearTimer.isActive()) {
		_clearTimer.callOnce(kRequestTimeLimit * 2);
	}

	list.match([&](const MTPDmessages_messagesNotModified &data) {
	}, [&](const auto &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		const auto &messages = data.vmessages().v;
		if (messages.isEmpty()) {
			clearNotSending(history);
			return;
		}
		auto received = base::flat_set<not_null<HistoryItem*>>();
		auto clear = base::flat_set<not_null<HistoryItem*>>();
		auto &list = _data.emplace(history, List()).first->second;
		for (const auto &message : messages) {
			if (const auto item = append(history, list, message)) {
				received.emplace(item);
			}
		}
		for (const auto &owned : list.items) {
			const auto item = owned.get();
			if (!item->isSending() && !received.contains(item)) {
				clear.emplace(item);
			}
		}
		updated(history, received, clear);
	});
}

HistoryItem *ScheduledMessages::append(
		not_null<History*> history,
		List &list,
		const MTPMessage &message) {
	const auto id = message.match([&](const auto &data) {
		return data.vid().v;
	});
	const auto i = list.itemById.find(id);
	if (i != end(list.itemById)) {
		const auto existing = i->second;
		message.match([&](const MTPDmessage &data) {
			// Scheduled messages never have an edit date,
			// so if we receive a flag about it,
			// probably this message was edited.
			if (data.is_edit_hide()) {
				existing->applyEdition(data);
			}
			existing->updateSentContent({
				qs(data.vmessage()),
				Api::EntitiesFromMTP(
					_session,
					data.ventities().value_or_empty())
			}, data.vmedia());
			existing->updateReplyMarkup(data.vreply_markup());
			existing->updateForwardedInfo(data.vfwd_from());
			existing->updateDate(data.vdate().v);
			history->owner().requestItemTextRefresh(existing);
		}, [&](const auto &data) {});
		return existing;
	}

	const auto item = _session->data().addNewMessage(
		PrepareMessage(message, history->nextNonHistoryEntryId()),
		MTPDmessage_ClientFlags(),
		NewMessageType::Existing);
	if (!item || item->history() != history) {
		LOG(("API Error: Bad data received in scheduled messages."));
		return nullptr;
	}
	list.items.emplace_back(item);
	list.itemById.emplace(id, item);
	list.idByItem.emplace(item, id);
	return item;
}

void ScheduledMessages::clearNotSending(not_null<History*> history) {
	const auto i = _data.find(history);
	if (i == end(_data)) {
		return;
	}
	auto clear = base::flat_set<not_null<HistoryItem*>>();
	for (const auto &owned : i->second.items) {
		if (!owned->isSending() && !owned->hasFailed()) {
			clear.emplace(owned.get());
		}
	}
	updated(history, {}, clear);
}

void ScheduledMessages::updated(
		not_null<History*> history,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear) {
	if (!clear.empty()) {
		for (const auto item : clear) {
			item->destroy();
		}
	}
	const auto i = _data.find(history);
	if (i != end(_data)) {
		sort(i->second);
	}
	if (!added.empty() || !clear.empty()) {
		_updates.fire_copy(history);
	}
}

void ScheduledMessages::sort(List &list) {
	ranges::sort(list.items, ranges::less(), &HistoryItem::position);
}

void ScheduledMessages::remove(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	const auto i = _data.find(history);
	Assert(i != end(_data));
	auto &list = i->second;

	if (!item->isSending() && !item->hasFailed()) {
		const auto j = list.idByItem.find(item);
		Assert(j != end(list.idByItem));
		list.itemById.remove(j->second);
		list.idByItem.erase(j);
	}
	const auto k = ranges::find(list.items, item, &OwnedItem::get);
	Assert(k != list.items.end());
	k->release();
	list.items.erase(k);

	if (list.items.empty()) {
		_data.erase(i);
	}
	_updates.fire_copy(history);
}

int32 ScheduledMessages::countListHash(const List &list) const {
	using namespace Api;

	auto hash = HashInit();
	auto &&serverside = ranges::views::all(
		list.items
	) | ranges::views::filter([](const OwnedItem &item) {
		return !item->isSending() && !item->hasFailed();
	}) | ranges::views::reverse;
	for (const auto &item : serverside) {
		const auto j = list.idByItem.find(item.get());
		HashUpdate(hash, j->second);
		if (const auto edited = item->Get<HistoryMessageEdited>()) {
			HashUpdate(hash, edited->date);
		} else {
			HashUpdate(hash, int32(0));
		}
		HashUpdate(hash, item->date());
	}
	return HashFinalize(hash);
}

} // namespace Data
