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
	if (list.showedAll || !TooEarlyForRequest(list.received)) {
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
		| MessageFlag::Local;
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

	return true;
}

bool SponsoredMessages::canHaveFor(not_null<History*> history) const {
	return history->isChannel();
}

void SponsoredMessages::request(not_null<History*> history) {
	if (!canHaveFor(history)) {
		return;
	}
	auto &request = _requests[history];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	{
		const auto it = _data.find(history);
		if (it != end(_data)) {
			auto &list = it->second;
			// Don't rebuild currently displayed messages.
			const auto proj = [](const Entry &e) {
				return e.item != nullptr;
			};
			if (ranges::any_of(list.entries, proj)) {
				return;
			}
		}
	}
	const auto channel = history->peer->asChannel();
	Assert(channel != nullptr);
	request.requestId = _session->api().request(
		MTPchannels_GetSponsoredMessages(
			channel->inputChannel)
	).done([=](const MTPmessages_sponsoredMessages &result) {
		parse(history, result);
	}).fail([=] {
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
		auto &list = _data.emplace(history, List()).first->second;
		list.entries.clear();
		list.received = crl::now();
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
			.msgId = data.vchannel_post().value_or_empty(),
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

const SponsoredMessages::Entry *SponsoredMessages::find(
		const FullMsgId &fullId) const {
	if (!fullId.channel) {
		return nullptr;
	}
	const auto history = _session->data().history(
		peerFromChannel(fullId.channel));
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return nullptr;
	}
	auto &list = it->second;
	const auto entryIt = ranges::find_if(list.entries, [&](const Entry &e) {
		return e.item->fullId() == fullId;
	});
	if (entryIt == end(list.entries)) {
		return nullptr;
	}
	return &*entryIt;
}

void SponsoredMessages::view(const FullMsgId &fullId) {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return;
	}
	const auto randomId = entryPtr->sponsored.randomId;
	auto &request = _viewRequests[randomId];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	const auto channel = entryPtr->item->history()->peer->asChannel();
	Assert(channel != nullptr);
	request.requestId = _session->api().request(
		MTPchannels_ViewSponsoredMessage(
			channel->inputChannel,
			MTP_bytes(randomId))
	).done([=] {
		auto &request = _viewRequests[randomId];
		request.lastReceived = crl::now();
		request.requestId = 0;
	}).fail([=] {
		_viewRequests.remove(randomId);
	}).send();
}

MsgId SponsoredMessages::channelPost(const FullMsgId &fullId) const {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return ShowAtUnreadMsgId;
	}
	const auto msgId = entryPtr->sponsored.msgId;
	return msgId ? msgId : ShowAtUnreadMsgId;
}

} // namespace Data
