/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_sponsored_messages.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kRequestTimeLimit = 5 * 60 * crl::time(1000);

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

} // namespace

SponsoredMessages::SponsoredMessages(not_null<Session*> owner)
: _session(&owner->session())
, _clearTimer([=] { clearOldRequests(); }) {
}

SponsoredMessages::~SponsoredMessages() {
	for (const auto &request : _requests) {
		_session->api().request(request.second.requestId).cancel();
	}
}

void SponsoredMessages::clearOldRequests() {
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

bool SponsoredMessages::append(not_null<History*> history) {
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return false;
	}
	auto &list = it->second;
	if (list.showedAll) {
		return false;
	}

	const auto entryIt = ranges::find_if(list.entries, [](const Entry &e) {
		return e.item == nullptr;
	});
	if (entryIt == end(list.entries)) {
		list.showedAll = true;
		return false;
	}

	const auto flags = MessageFlags(0)
		| (history->isChannel() ? MessageFlag::Post : MessageFlags(0))
		| MessageFlag::HasFromId
		| MessageFlag::IsSponsored
		| MessageFlag::LocalHistoryEntry;
	auto local = history->addNewLocalMessage(
		_session->data().nextLocalMessageId(),
		flags,
		UserId(0),
		MsgId(0),
		HistoryItem::NewMessageDate(0),
		entryIt->sponsored.fromId,
		QString(),
		entryIt->sponsored.textWithEntities,
		MTP_messageMediaEmpty(),
		HistoryMessageMarkupData());
	entryIt->item.reset(std::move(local));

	// Since sponsored posts are only created on demand for display,
	// we can send a request to view immediately.
	view(entryIt);

	return true;
}

void SponsoredMessages::request(not_null<History*> history) {
	auto &request = _requests[history];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	request.requestId = _session->api().request(
		MTPchannels_GetSponsoredMessages(
			_session->data().channel(history->channelId())->inputChannel
	)).done([=](const MTPmessages_sponsoredMessages &result) {
		parse(history, result);
	}).fail([=](const MTP::Error &error) {
		_requests.remove(history);
	}).send();
}

void SponsoredMessages::parse(
		not_null<History*> history,
		const MTPmessages_sponsoredMessages &list) {
	auto &request = _requests[history];
	request.lastReceived = crl::now();
	request.requestId = 0;
	if (!_clearTimer.isActive()) {
		_clearTimer.callOnce(kRequestTimeLimit * 2);
	}

	list.match([&](const MTPDmessages_sponsoredMessages &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		const auto &messages = data.vmessages().v;
		if (messages.isEmpty()) {
			return;
		}
		auto &list = _data.emplace(history, List()).first->second;
		list.entries.clear();
		for (const auto &message : messages) {
			append(history, list, message);
		}
	});
}

void SponsoredMessages::append(
		not_null<History*> history,
		List &list,
		const MTPSponsoredMessage &message) {
	message.match([&](const MTPDsponsoredMessage &data) {
		const auto randomId = data.vrandom_id().v;
		auto sharedMessage = SponsoredMessage{
			.randomId = randomId,
			.fromId = peerFromMTP(data.vfrom_id()),
			.textWithEntities = {
				.text = qs(data.vmessage()),
				.entities = Api::EntitiesFromMTP(
					_session,
					data.ventities().value_or_empty()),
			},
			.history = history,
			//.msgId = data.vchannel_post().value_or_empty(),
		};
		list.entries.push_back({ nullptr, std::move(sharedMessage) });
	});
}

void SponsoredMessages::clearItems(not_null<History*> history) {
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return;
	}
	auto &list = it->second;
	for (auto &entry : list.entries) {
		entry.item.reset();
	}
	list.showedAll = false;
}

void SponsoredMessages::view(const std::vector<Entry>::iterator entryIt) {
	const auto randomId = entryIt->sponsored.randomId;
	auto &request = _viewRequests[randomId];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	const auto history = entryIt->sponsored.history;
	if (!history) {
		return;
	}
	request.requestId = _session->api().request(
		MTPchannels_ViewSponsoredMessage(
			_session->data().channel(history->channelId())->inputChannel,
			MTP_bytes(randomId)
	)).done([=] {
		auto &request = _viewRequests[randomId];
		request.lastReceived = crl::now();
		request.requestId = 0;
	}).fail([=](const MTP::Error &error) {
		_viewRequests.remove(randomId);
	}).send();
}

MsgId SponsoredMessages::channelPost(const FullMsgId &fullId) const {
	const auto history = _session->data().history(
		peerFromChannel(fullId.channel));
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return ShowAtUnreadMsgId;
	}
	auto &list = it->second;
	const auto entryIt = ranges::find_if(list.entries, [&](const Entry &e) {
		return e.item->fullId() == fullId;
	});
	if (entryIt == end(list.entries)) {
		return ShowAtUnreadMsgId;
	}
	const auto msgId = entryIt->sponsored.msgId;
	return msgId ? msgId : ShowAtUnreadMsgId;
}

} // namespace Data
