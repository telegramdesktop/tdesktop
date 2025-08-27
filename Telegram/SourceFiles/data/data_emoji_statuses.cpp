/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_emoji_statuses.h"

#include "main/main_session.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_document.h"
#include "data/data_wall_paper.h"
#include "data/stickers/data_stickers.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "base/call_delayed.h"
#include "apiwrap.h"
#include "ui/controls/tabbed_search.h"

namespace Data {
namespace {

constexpr auto kRefreshDefaultListEach = 60 * 60 * crl::time(1000);
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kMaxTimeout = 6 * 60 * 60 * crl::time(1000);

[[nodiscard]] EmojiStatusCollectible ParseEmojiStatusCollectible(
		const MTPDemojiStatusCollectible &data) {
	return EmojiStatusCollectible{
		.id = data.vcollectible_id().v,
		.documentId = data.vdocument_id().v,
		.title = qs(data.vtitle()),
		.slug = qs(data.vslug()),
		.patternDocumentId = data.vpattern_document_id().v,
		.centerColor = Ui::ColorFromSerialized(data.vcenter_color()),
		.edgeColor = Ui::ColorFromSerialized(data.vedge_color()),
		.patternColor = Ui::ColorFromSerialized(data.vpattern_color()),
		.textColor = Ui::ColorFromSerialized(data.vtext_color()),
	};
}

} // namespace

EmojiStatuses::EmojiStatuses(not_null<Session*> owner)
: _owner(owner)
, _clearingTimer([=] { processClearing(); }) {
	refreshDefault();
	refreshColored();

	base::timer_each(
		kRefreshDefaultListEach
	) | rpl::start_with_next([=] {
		refreshDefault();
		refreshChannelDefault();
	}, _lifetime);
}

EmojiStatuses::~EmojiStatuses() = default;

Main::Session &EmojiStatuses::session() const {
	return _owner->session();
}

void EmojiStatuses::refreshRecent() {
	requestRecent();
}

void EmojiStatuses::refreshDefault() {
	requestDefault();
}

void EmojiStatuses::refreshColored() {
	requestColored();
}

void EmojiStatuses::refreshChannelDefault() {
	requestChannelDefault();
}

void EmojiStatuses::refreshChannelColored() {
	requestChannelColored();
}

void EmojiStatuses::refreshCollectibles() {
	requestCollectibles();
}

void EmojiStatuses::refreshRecentDelayed() {
	if (_recentRequestId || _recentRequestScheduled) {
		return;
	}
	_recentRequestScheduled = true;
	base::call_delayed(kRecentRequestTimeout, &_owner->session(), [=] {
		if (_recentRequestScheduled) {
			requestRecent();
		}
	});
}

const std::vector<EmojiStatusId> &EmojiStatuses::list(Type type) const {
	switch (type) {
	case Type::Recent: return _recent;
	case Type::Default: return _default;
	case Type::Colored: return _colored;
	case Type::ChannelDefault: return _channelDefault;
	case Type::ChannelColored: return _channelColored;
	case Type::Collectibles: return _collectibles;
	}
	Unexpected("Type in EmojiStatuses::list.");
}

EmojiStatusData EmojiStatuses::parse(const MTPEmojiStatus &status) {
	return status.match([](const MTPDemojiStatus &data) {
		return EmojiStatusData{
			.id = { .documentId = data.vdocument_id().v },
			.until = data.vuntil().value_or_empty(),
		};
	}, [&](const MTPDemojiStatusCollectible &data) {
		const auto collectibleId = data.vcollectible_id().v;
		auto &collectible = _collectibleData[collectibleId];
		if (!collectible) {
			collectible = std::make_shared<EmojiStatusCollectible>(
				ParseEmojiStatusCollectible(data));
		}
		return EmojiStatusData{
			.id = { .collectible = collectible },
			.until = data.vuntil().value_or_empty(),
		};
	}, [](const MTPDinputEmojiStatusCollectible &) {
		return EmojiStatusData();
	}, [](const MTPDemojiStatusEmpty &) {
		return EmojiStatusData();
	});
}

rpl::producer<> EmojiStatuses::recentUpdates() const {
	return _recentUpdated.events();
}

rpl::producer<> EmojiStatuses::defaultUpdates() const {
	return _defaultUpdated.events();
}

rpl::producer<> EmojiStatuses::channelDefaultUpdates() const {
	return _channelDefaultUpdated.events();
}

rpl::producer<> EmojiStatuses::collectiblesUpdates() const {
	return _collectiblesUpdated.events();
}

void EmojiStatuses::registerAutomaticClear(
		not_null<PeerData*> peer,
		TimeId until) {
	if (!until) {
		_clearing.remove(peer);
		if (_clearing.empty()) {
			_clearingTimer.cancel();
		}
	} else if (auto &already = _clearing[peer]; already != until) {
		already = until;
		const auto i = ranges::min_element(_clearing, {}, [](auto &&pair) {
			return pair.second;
		});
		if (i->first == peer) {
			const auto now = base::unixtime::now();
			if (now < until) {
				processClearingIn(until - now);
			} else {
				processClearing();
			}
		}
	}
}

auto EmojiStatuses::emojiGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestEmojiGroups();
	return _emojiGroups.data.value();
}

auto EmojiStatuses::statusGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestStatusGroups();
	return _statusGroups.data.value();
}

auto EmojiStatuses::stickerGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestStickerGroups();
	return _stickerGroups.data.value();
}

auto EmojiStatuses::profilePhotoGroupsValue() const
-> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestProfilePhotoGroups();
	return _profilePhotoGroups.data.value();
}

void EmojiStatuses::requestEmojiGroups() {
	requestGroups(
		&_emojiGroups,
		MTPmessages_GetEmojiGroups(MTP_int(_emojiGroups.hash)));

}

void EmojiStatuses::requestStatusGroups() {
	requestGroups(
		&_statusGroups,
		MTPmessages_GetEmojiStatusGroups(MTP_int(_statusGroups.hash)));
}

void EmojiStatuses::requestStickerGroups() {
	requestGroups(
		&_stickerGroups,
		MTPmessages_GetEmojiStickerGroups(MTP_int(_stickerGroups.hash)));
}

void EmojiStatuses::requestProfilePhotoGroups() {
	requestGroups(
		&_profilePhotoGroups,
		MTPmessages_GetEmojiProfilePhotoGroups(
			MTP_int(_profilePhotoGroups.hash)));
}

[[nodiscard]] std::vector<Ui::EmojiGroup> GroupsFromTL(
		const MTPDmessages_emojiGroups &data) {
	const auto &list = data.vgroups().v;
	auto result = std::vector<Ui::EmojiGroup>();
	result.reserve(list.size());
	for (const auto &group : list) {
		group.match([&](const MTPDemojiGroupPremium &data) {
			result.push_back({
				.iconId = QString::number(data.vicon_emoji_id().v),
				.type = Ui::EmojiGroupType::Premium,
			});
		}, [&](const auto &data) {
			auto emoticons = ranges::views::all(
				data.vemoticons().v
			) | ranges::views::transform([](const MTPstring &emoticon) {
				return qs(emoticon);
			}) | ranges::to_vector;
			result.push_back({
				.iconId = QString::number(data.vicon_emoji_id().v),
				.emoticons = std::move(emoticons),
				.type = (MTPDemojiGroupGreeting::Is<decltype(data)>()
					? Ui::EmojiGroupType::Greeting
					: Ui::EmojiGroupType::Normal),
			});
		});
	}
	return result;
}

template <typename Request>
void EmojiStatuses::requestGroups(
		not_null<GroupsType*> type,
		Request &&request) {
	if (type->requestId) {
		return;
	}
	type->requestId = _owner->session().api().request(
		std::forward<Request>(request)
	).done([=](const MTPmessages_EmojiGroups &result) {
		type->requestId = 0;
		result.match([&](const MTPDmessages_emojiGroups &data) {
			type->hash = data.vhash().v;
			type->data = GroupsFromTL(data);
		}, [](const MTPDmessages_emojiGroupsNotModified&) {
		});
	}).fail([=] {
		type->requestId = 0;
	}).send();
}

void EmojiStatuses::processClearing() {
	auto minWait = TimeId(0);
	const auto now = base::unixtime::now();
	auto clearing = base::take(_clearing);
	for (auto i = begin(clearing); i != end(clearing);) {
		const auto until = i->second;
		if (now < until) {
			const auto wait = (until - now);
			if (!minWait || minWait > wait) {
				minWait = wait;
			}
			++i;
		} else {
			i->first->setEmojiStatus(EmojiStatusId());
			i = clearing.erase(i);
		}
	}
	if (_clearing.empty()) {
		_clearing = std::move(clearing);
	} else {
		for (const auto &[user, until] : clearing) {
			_clearing.emplace(user, until);
		}
	}
	if (minWait) {
		processClearingIn(minWait);
	} else {
		_clearingTimer.cancel();
	}
}

std::vector<EmojiStatusId> EmojiStatuses::parse(
		const MTPDaccount_emojiStatuses &data) {
	const auto &list = data.vstatuses().v;
	auto result = std::vector<EmojiStatusId>();
	result.reserve(list.size());
	for (const auto &status : list) {
		const auto parsed = parse(status);
		if (!parsed.id) {
			LOG(("API Error: empty status in account.emojiStatuses."));
		} else {
			result.push_back(parsed.id);
		}
	}
	return result;
}

void EmojiStatuses::processClearingIn(TimeId wait) {
	const auto waitms = wait * crl::time(1000);
	_clearingTimer.callOnce(std::min(waitms, kMaxTimeout));
}

void EmojiStatuses::requestRecent() {
	if (_recentRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_recentRequestScheduled = false;
	_recentRequestId = api.request(MTPaccount_GetRecentEmojiStatuses(
		MTP_long(_recentHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_recentRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateRecent(data);
		}, [](const MTPDaccount_emojiStatusesNotModified&) {
		});
	}).fail([=] {
		_recentRequestId = 0;
		_recentHash = 0;
	}).send();
}

void EmojiStatuses::requestDefault() {
	if (_defaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_defaultRequestId = api.request(MTPaccount_GetDefaultEmojiStatuses(
		MTP_long(_defaultHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_defaultRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateDefault(data);
		}, [&](const MTPDaccount_emojiStatusesNotModified &) {
		});
	}).fail([=] {
		_defaultRequestId = 0;
		_defaultHash = 0;
	}).send();
}

void EmojiStatuses::requestColored() {
	if (_coloredRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_coloredRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiDefaultStatuses(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_coloredRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateColored(data);
			refreshCollectibles();
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_coloredRequestId = 0;
	}).send();
}

void EmojiStatuses::requestChannelDefault() {
	if (_channelDefaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_channelDefaultRequestId = api.request(MTPaccount_GetDefaultEmojiStatuses(
		MTP_long(_channelDefaultHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_channelDefaultRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateChannelDefault(data);
		}, [&](const MTPDaccount_emojiStatusesNotModified &) {
		});
	}).fail([=] {
		_channelDefaultRequestId = 0;
		_channelDefaultHash = 0;
	}).send();
}

void EmojiStatuses::requestChannelColored() {
	if (_channelColoredRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_channelColoredRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiChannelDefaultStatuses(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_channelColoredRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateChannelColored(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_channelColoredRequestId = 0;
	}).send();
}

void EmojiStatuses::requestCollectibles() {
	if (_collectiblesRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_collectiblesRequestId = api.request(
		MTPaccount_GetCollectibleEmojiStatuses(MTP_long(_collectiblesHash))
	).done([=](const MTPaccount_EmojiStatuses &result) {
		_collectiblesRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateCollectibles(data);
		}, [&](const MTPDaccount_emojiStatusesNotModified &) {
		});
	}).fail([=] {
		_collectiblesRequestId = 0;
		_collectiblesHash = 0;
	}).send();
}

void EmojiStatuses::updateRecent(const MTPDaccount_emojiStatuses &data) {
	_recentHash = data.vhash().v;
	_recent = parse(data);
	_recentUpdated.fire({});
}

void EmojiStatuses::updateDefault(const MTPDaccount_emojiStatuses &data) {
	_defaultHash = data.vhash().v;
	_default = parse(data);
	_defaultUpdated.fire({});
}

void EmojiStatuses::updateColored(const MTPDmessages_stickerSet &data) {
	const auto &list = data.vdocuments().v;
	_colored.clear();
	_colored.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		_colored.push_back({
			.documentId = _owner->processDocument(sticker)->id,
		});
	}
	_coloredUpdated.fire({});
}

void EmojiStatuses::updateChannelDefault(
		const MTPDaccount_emojiStatuses &data) {
	_channelDefaultHash = data.vhash().v;
	_channelDefault = parse(data);
	_channelDefaultUpdated.fire({});
}

void EmojiStatuses::updateChannelColored(
		const MTPDmessages_stickerSet &data) {
	const auto &list = data.vdocuments().v;
	_channelColored.clear();
	_channelColored.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		_channelColored.push_back({
			.documentId = _owner->processDocument(sticker)->id,
		});
	}
	_channelColoredUpdated.fire({});
}

void EmojiStatuses::updateCollectibles(
	const MTPDaccount_emojiStatuses &data) {
	_collectiblesHash = data.vhash().v;
	_collectibles = parse(data);
	_collectiblesUpdated.fire({});
}

void EmojiStatuses::set(EmojiStatusId id, TimeId until) {
	set(_owner->session().user(), id, until);
}

void EmojiStatuses::set(
		not_null<PeerData*> peer,
		EmojiStatusId id,
		TimeId until) {
	auto &api = _owner->session().api();
	auto &requestId = _sentRequests[peer];
	if (requestId) {
		api.request(base::take(requestId)).cancel();
	}
	peer->setEmojiStatus(id, until);
	const auto send = [&](auto &&request) {
		requestId = api.request(
			std::move(request)
		).done([=] {
			_sentRequests.remove(peer);
		}).fail([=] {
			_sentRequests.remove(peer);
		}).send();
	};
	using EFlag = MTPDemojiStatus::Flag;
	using CFlag = MTPDinputEmojiStatusCollectible::Flag;
	const auto status = !id
		? MTP_emojiStatusEmpty()
		: id.collectible
		? MTP_inputEmojiStatusCollectible(
			MTP_flags(until ? CFlag::f_until : CFlag()),
			MTP_long(id.collectible->id),
			MTP_int(until))
		: MTP_emojiStatus(
			MTP_flags(until ? EFlag::f_until : EFlag()),
			MTP_long(id.documentId),
			MTP_int(until));
	if (peer->isSelf()) {
		send(MTPaccount_UpdateEmojiStatus(status));
	} else if (const auto channel = peer->asChannel()) {
		send(MTPchannels_UpdateEmojiStatus(channel->inputChannel, status));
	}
}

EmojiStatusId EmojiStatuses::fromUniqueGift(
		const Data::UniqueGift &gift) {
	const auto collectibleId = gift.id;
	auto &collectible = _collectibleData[collectibleId];
	if (!collectible) {
		collectible = std::make_shared<EmojiStatusCollectible>(
			EmojiStatusCollectible{
				.id = gift.id,
				.documentId = gift.model.document->id,
				.title = Data::UniqueGiftName(gift),
				.slug = gift.slug,
				.patternDocumentId = gift.pattern.document->id,
				.centerColor = gift.backdrop.centerColor,
				.edgeColor = gift.backdrop.edgeColor,
				.patternColor = gift.backdrop.patternColor,
				.textColor = gift.backdrop.textColor,
			});
	}
	return { .collectible = collectible };
}

EmojiStatusCollectible *EmojiStatuses::collectibleInfo(CollectibleId id) {
	const auto i = _collectibleData.find(id);
	return (i != end(_collectibleData)) ? i->second.get() : nullptr;
}

} // namespace Data
