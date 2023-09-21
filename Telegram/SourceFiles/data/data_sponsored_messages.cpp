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
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/image/image_location_factory.h"
#include "ui/text/text_utilities.h" // Ui::Text::RichLangValue.

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
	if (list.showedAll
		|| !TooEarlyForRequest(list.received)
		|| list.postsBetween) {
		return false;
	}

	const auto entryIt = ranges::find_if(list.entries, [](const Entry &e) {
		return e.item == nullptr;
	});
	if (entryIt == end(list.entries)) {
		list.showedAll = true;
		return false;
	}

	entryIt->item.reset(history->addNewLocalMessage(
		_session->data().nextLocalMessageId(),
		entryIt->sponsored.from,
		entryIt->sponsored.textWithEntities));

	return true;
}

void SponsoredMessages::inject(
		not_null<History*> history,
		MsgId injectAfterMsgId,
		int betweenHeight,
		int fallbackWidth) {
	if (!canHaveFor(history)) {
		return;
	}
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return;
	}
	auto &list = it->second;
	if (!list.postsBetween || (list.entries.size() == list.injectedCount)) {
		return;
	}

	while (true) {
		const auto entryIt = ranges::find_if(list.entries, [](const auto &e) {
			return e.item == nullptr;
		});
		if (entryIt == end(list.entries)) {
			list.showedAll = true;
			return;
		}
		const auto lastView = (entryIt != begin(list.entries))
			? (entryIt - 1)->item->mainView()
			: (injectAfterMsgId == ShowAtUnreadMsgId)
			? history->firstUnreadMessage()
			: [&] {
				const auto message = history->peer->owner().message(
					history->peer->id,
					injectAfterMsgId);
				return message ? message->mainView() : nullptr;
			}();
		if (!lastView || !lastView->block()) {
			return;
		}

		auto summaryBetween = 0;
		auto summaryHeight = 0;

		using BlockPtr = std::unique_ptr<HistoryBlock>;
		using ViewPtr = std::unique_ptr<HistoryView::Element>;
		auto blockIt = ranges::find(
			history->blocks,
			lastView->block(),
			&BlockPtr::get);
		if (blockIt == end(history->blocks)) {
			return;
		}
		const auto messages = [&]() -> const std::vector<ViewPtr> & {
			return (*blockIt)->messages;
		};
		auto lastViewIt = ranges::find(messages(), lastView, &ViewPtr::get);
		while ((summaryBetween < list.postsBetween)
			|| (summaryHeight < betweenHeight)) {
			lastViewIt++;
			if (lastViewIt == end(messages())) {
				blockIt++;
				if (blockIt != end(history->blocks)) {
					lastViewIt = begin(messages());
				} else {
					return;
				}
			}
			summaryBetween++;
			const auto viewHeight = (*lastViewIt)->height();
			summaryHeight += viewHeight
				? viewHeight
				: (*lastViewIt)->resizeGetHeight(fallbackWidth);
		}
		const auto makedMessage = history->makeMessage(
			_session->data().nextLocalMessageId(),
			entryIt->sponsored.from,
			entryIt->sponsored.textWithEntities,
			(*lastViewIt)->data());
		entryIt->item.reset(makedMessage.get());
		history->addNewInTheMiddle(
			makedMessage.get(),
			std::distance(begin(history->blocks), blockIt),
			std::distance(begin(messages()), lastViewIt) + 1);
		messages().back().get()->setPendingResize();
		list.injectedCount++;
	}
}

bool SponsoredMessages::canHaveFor(not_null<History*> history) const {
	return history->peer->isChannel();
}

void SponsoredMessages::request(not_null<History*> history, Fn<void()> done) {
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
		if (done) {
			done();
		}
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
		if (const auto postsBetween = data.vposts_between()) {
			list.postsBetween = postsBetween->v;
			list.state = State::InjectToMiddle;
		} else {
			list.state = State::AppendToEnd;
		}
	}, [](const MTPDmessages_sponsoredMessagesEmpty &) {
	});
}

void SponsoredMessages::append(
		not_null<History*> history,
		List &list,
		const MTPSponsoredMessage &message) {
	const auto &data = message.data();
	const auto randomId = data.vrandom_id().v;
	const auto hash = qs(data.vchat_invite_hash().value_or_empty());
	const auto makeFrom = [&](
			not_null<PeerData*> peer,
			bool exactPost = false) {
		const auto channel = peer->asChannel();
		return SponsoredFrom{
			.peer = peer,
			.title = peer->name(),
			.isBroadcast = (channel && channel->isBroadcast()),
			.isMegagroup = (channel && channel->isMegagroup()),
			.isChannel = (channel != nullptr),
			.isPublic = (channel && channel->isPublic()),
			.isBot = (peer->isUser() && peer->asUser()->isBot()),
			.isExactPost = exactPost,
			.isRecommended = data.is_recommended(),
			.userpic = { .location = peer->userpicLocation() },
			.isForceUserpicDisplay = data.is_show_peer_photo(),
		};
	};
	const auto externalLink = data.vwebpage()
		? qs(data.vwebpage()->data().vurl())
		: QString();
	const auto userpicFromPhoto = [&](const MTPphoto &photo) {
		return photo.match([&](const MTPDphoto &data) {
			for (const auto &size : data.vsizes().v) {
				const auto result = Images::FromPhotoSize(
					_session,
					data,
					size);
				if (result.location.valid()) {
					return result;
				}
			}
			return ImageWithLocation{};
		}, [](const MTPDphotoEmpty &) {
			return ImageWithLocation{};
		});
	};
	const auto from = [&]() -> SponsoredFrom {
		if (const auto webpage = data.vwebpage()) {
			const auto &data = webpage->data();
			auto userpic = data.vphoto()
				? userpicFromPhoto(*data.vphoto())
				: ImageWithLocation{};
			return SponsoredFrom{
				.title = qs(data.vsite_name()),
				.externalLink = externalLink,
				.userpic = std::move(userpic),
				.isForceUserpicDisplay = message.data().is_show_peer_photo(),
			};
		} else if (const auto fromId = data.vfrom_id()) {
			return makeFrom(
				_session->data().peer(peerFromMTP(*fromId)),
				(data.vchannel_post() != nullptr));
		}
		Assert(data.vchat_invite());
		return data.vchat_invite()->match([&](const MTPDchatInvite &data) {
			auto userpic = userpicFromPhoto(data.vphoto());
			return SponsoredFrom{
				.title = qs(data.vtitle()),
				.isBroadcast = data.is_broadcast(),
				.isMegagroup = data.is_megagroup(),
				.isChannel = data.is_channel(),
				.isPublic = data.is_public(),
				.userpic = std::move(userpic),
				.isForceUserpicDisplay = message.data().is_show_peer_photo(),
			};
		}, [&](const MTPDchatInviteAlready &data) {
			const auto chat = _session->data().processChat(data.vchat());
			if (const auto channel = chat->asChannel()) {
				channel->clearInvitePeek();
			}
			return makeFrom(chat);
		}, [&](const MTPDchatInvitePeek &data) {
			const auto chat = _session->data().processChat(data.vchat());
			if (const auto channel = chat->asChannel()) {
				channel->setInvitePeek(hash, data.vexpires().v);
			}
			return makeFrom(chat);
		});
	}();
	auto sponsorInfo = data.vsponsor_info()
		? tr::lng_sponsored_info_submenu(
			tr::now,
			lt_text,
			{ .text = qs(*data.vsponsor_info()) },
			Ui::Text::RichLangValue)
		: TextWithEntities();
	auto additionalInfo = TextWithEntities::Simple(
		data.vadditional_info() ? qs(*data.vadditional_info()) : QString());
	auto sharedMessage = SponsoredMessage{
		.randomId = randomId,
		.from = from,
		.textWithEntities = {
			.text = qs(data.vmessage()),
			.entities = Api::EntitiesFromMTP(
				_session,
				data.ventities().value_or_empty()),
		},
		.history = history,
		.msgId = data.vchannel_post().value_or_empty(),
		.chatInviteHash = hash,
		.externalLink = externalLink,
		.sponsorInfo = std::move(sponsorInfo),
		.additionalInfo = std::move(additionalInfo),
	};
	list.entries.push_back({ nullptr, std::move(sharedMessage) });
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
	list.injectedCount = 0;
}

const SponsoredMessages::Entry *SponsoredMessages::find(
		const FullMsgId &fullId) const {
	if (!peerIsChannel(fullId.peer)) {
		return nullptr;
	}
	const auto history = _session->data().history(fullId.peer);
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return nullptr;
	}
	auto &list = it->second;
	const auto entryIt = ranges::find_if(list.entries, [&](const Entry &e) {
		return e.item && e.item->fullId() == fullId;
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

SponsoredMessages::Details SponsoredMessages::lookupDetails(
		const FullMsgId &fullId) const {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return {};
	}
	const auto &data = entryPtr->sponsored;
	const auto &hash = data.chatInviteHash;

	using InfoList = std::vector<TextWithEntities>;
	const auto info = (!data.sponsorInfo.text.isEmpty()
			&& !data.additionalInfo.text.isEmpty())
		? InfoList{ data.sponsorInfo, data.additionalInfo }
		: !data.sponsorInfo.text.isEmpty()
		? InfoList{ data.sponsorInfo }
		: !data.additionalInfo.text.isEmpty()
		? InfoList{ data.additionalInfo }
		: InfoList{};
	return {
		.hash = hash.isEmpty() ? std::nullopt : std::make_optional(hash),
		.peer = data.from.peer,
		.msgId = data.msgId,
		.info = std::move(info),
		.externalLink = data.externalLink,
	};
}

void SponsoredMessages::clicked(const FullMsgId &fullId) {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return;
	}
	const auto randomId = entryPtr->sponsored.randomId;
	const auto channel = entryPtr->item->history()->peer->asChannel();
	Assert(channel != nullptr);
	_session->api().request(MTPchannels_ClickSponsoredMessage(
		channel->inputChannel,
		MTP_bytes(randomId)
	)).send();
}

SponsoredMessages::State SponsoredMessages::state(
		not_null<History*> history) const {
	const auto it = _data.find(history);
	return (it == end(_data)) ? State::None : it->second.state;
}

} // namespace Data
