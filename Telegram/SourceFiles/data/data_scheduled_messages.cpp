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
: _session(&owner->session()) {
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
	if (request.requestId) {
		return;
	}
	request.requestId = _session->api().request(
		MTPmessages_GetScheduledHistory(
			history->peer->input,
			MTP_int(request.hash))
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
	request.requestId = 0;

	auto element = _data.find(history);
	list.match([&](const MTPDmessages_messagesNotModified &data) {
	}, [&](const auto &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		const auto &messages = data.vmessages().v;
		if (messages.isEmpty()) {
			return;
		}
		element = _data.emplace(history, List()).first;
		auto &list = element->second;
		for (const auto &message : messages) {
			append(history, list, message);
		}
		if (!list.items.empty()) {
			sort(list);
		} else {
			_data.erase(element);
			element = end(_data);
		}
		_updates.fire_copy(history);
	});

	request.hash = (element != end(_data))
		? countListHash(element->second)
		: 0;
	if (!request.requestId && !request.hash) {
		_requests.remove(history);
	}
}

void ScheduledMessages::append(
		not_null<History*> history,
		List &list,
		const MTPMessage &message) {
	const auto id = message.match([&](const auto &data) {
		return data.vid().v;
	});
	if (list.itemById.find(id) != end(list.itemById)) {
		return;
	}

	const auto item = _session->data().addNewMessage(
		PrepareMessage(message, history->nextNonHistoryEntryId()),
		MTPDmessage_ClientFlags(),
		NewMessageType::Existing);
	if (!item || item->history() != history) {
		LOG(("API Error: Bad data received in scheduled messages."));
		return;
	}
	list.items.emplace_back(item);
	list.itemById.emplace(id, item);
	list.idByItem.emplace(item, id);
}

void ScheduledMessages::sort(List &list) {
	ranges::sort(list.items, ranges::less(), &HistoryItem::position);
}

void ScheduledMessages::remove(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	const auto i = _data.find(history);
	Assert(i != end(_data));
	auto &list = i->second;

	const auto j = list.idByItem.find(item);
	Assert(j != end(list.idByItem));
	list.itemById.remove(j->second);
	list.idByItem.erase(j);

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
	for (const auto &item : list.items | ranges::view::reverse) {
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
