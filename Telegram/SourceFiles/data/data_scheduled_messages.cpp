/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_scheduled_messages.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "api/api_hash.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRequestTimeLimit = 60 * crl::time(1000);

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

MTPMessage PrepareMessage(const MTPMessage &message, MsgId id) {
	return message.match([&](const MTPDmessageEmpty &) {
		return MTP_messageEmpty(MTP_int(id));
	}, [&](const MTPDmessageService &data) {
		return MTP_messageService(
			MTP_flags(data.vflags().v
				| MTPDmessageService::Flag(
					MTPDmessage::Flag::f_from_scheduled)),
			MTP_int(id),
			MTP_int(data.vfrom_id().value_or_empty()),
			data.vto_id(),
			MTP_int(data.vreply_to_msg_id().value_or_empty()),
			data.vdate(),
			data.vaction());
	}, [&](const MTPDmessage &data) {
		const auto fwdFrom = data.vfwd_from();
		const auto media = data.vmedia();
		const auto markup = data.vreply_markup();
		const auto entities = data.ventities();
		return MTP_message(
			MTP_flags(data.vflags().v | MTPDmessage::Flag::f_from_scheduled),
			MTP_int(id),
			MTP_int(data.vfrom_id().value_or_empty()),
			data.vto_id(),
			fwdFrom ? *fwdFrom : MTPMessageFwdHeader(),
			MTP_int(data.vvia_bot_id().value_or_empty()),
			MTP_int(data.vreply_to_msg_id().value_or_empty()),
			data.vdate(),
			data.vmessage(),
			media ? *media : MTPMessageMedia(),
			markup ? *markup : MTPReplyMarkup(),
			entities ? *entities : MTPVector<MTPMessageEntity>(),
			MTP_int(data.vviews().value_or_empty()),
			MTP_int(data.vedit_date().value_or_empty()),
			MTP_bytes(data.vpost_author().value_or_empty()),
			MTP_long(data.vgrouped_id().value_or_empty()),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>());
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

int ScheduledMessages::count(not_null<History*> history) const {
	const auto i = _data.find(history);
	return (i != end(_data)) ? i->second.items.size() : 0;
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

	const auto history = item->history();
	auto &list = _data[history];
	Assert(!list.itemById.contains(item->id));
	Assert(!list.idByItem.contains(item));
	list.items.erase(
		ranges::remove(list.items, item.get(), &OwnedItem::get),
		end(list.items));
}

rpl::producer<> ScheduledMessages::updates(not_null<History*> history) {
	request(history);

	return _updates.events(
	) | rpl::filter([=](not_null<History*> value) {
		return (value == history);
	}) | rpl::map([] {
		return rpl::empty_value();
	});
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
	result.ids = ranges::view::all(
		list
	) | ranges::view::transform(
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
	}).fail([=](const RPCError &error) {
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
			existing->updateSentContent({
				qs(data.vmessage()),
				TextUtilities::EntitiesFromMTP(
					data.ventities().value_or_empty())
			}, data.vmedia());
			existing->updateReplyMarkup(data.vreply_markup());
			existing->updateForwardedInfo(data.vfwd_from());
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
	auto &&serverside = ranges::view::all(
		list.items
	) | ranges::view::filter([](const OwnedItem &item) {
		return !item->isSending() && !item->hasFailed();
	}) | ranges::view::reverse;
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
