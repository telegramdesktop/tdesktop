/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/gift_auctions.h"

#include "api/api_hash.h"
#include "api/api_premium.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Data {

GiftAuctions::GiftAuctions(not_null<Main::Session*> session)
: _session(session)
, _timer([=] { checkSubscriptions(); }) {
	crl::on_main(_session, [=] {
		rpl::merge(
			_session->data().chatsListChanges(),
			_session->data().chatsListLoadedEvents()
		) | rpl::filter(
			!rpl::mappers::_1
		) | rpl::take(1) | rpl::on_next([=] {
			requestActive();
		}, _lifetime);
	});
}

GiftAuctions::~GiftAuctions() = default;

rpl::producer<GiftAuctionState> GiftAuctions::state(const QString &slug) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		auto &entry = _map[slug];
		if (!entry) {
			entry = std::make_unique<Entry>();
		}
		const auto raw = entry.get();

		raw->changes.events() | rpl::on_next([=] {
			consumer.put_next_copy(raw->state);
		}, lifetime);

		const auto now = crl::now();
		if (raw->state.subscribedTill < 0
			|| raw->state.subscribedTill >= now) {
			consumer.put_next_copy(raw->state);
		} else if (raw->state.subscribedTill >= 0) {
			request(slug);
		}

		return lifetime;
	};
}

void GiftAuctions::apply(const MTPDupdateStarGiftAuctionState &data) {
	if (const auto entry = find(data.vgift_id().v)) {
		const auto was = myStateKey(entry->state);
		apply(entry, data.vstate());
		entry->changes.fire({});
		if (was != myStateKey(entry->state)) {
			_activeChanged.fire({});
		}
	} else {
		requestActive();
	}
}

void GiftAuctions::apply(const MTPDupdateStarGiftAuctionUserState &data) {
	if (const auto entry = find(data.vgift_id().v)) {
		const auto was = myStateKey(entry->state);
		apply(entry, data.vuser_state());
		entry->changes.fire({});
		if (was != myStateKey(entry->state)) {
			_activeChanged.fire({});
		}
	} else {
		requestActive();
	}
}

void GiftAuctions::requestAcquired(
		uint64 giftId,
		Fn<void(std::vector<Data::GiftAcquired>)> done) {
	Expects(done != nullptr);

	_session->api().request(MTPpayments_GetStarGiftAuctionAcquiredGifts(
		MTP_long(giftId)
	)).done([=](const MTPpayments_StarGiftAuctionAcquiredGifts &result) {
		const auto &data = result.data();

		const auto owner = &_session->data();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());

		const auto &list = data.vgifts().v;
		auto gifts = std::vector<Data::GiftAcquired>();
		gifts.reserve(list.size());
		for (const auto &gift : list) {
			const auto &data = gift.data();
			gifts.push_back({
				.to = owner->peer(peerFromMTP(data.vpeer())),
				.message = (data.vmessage()
					? Api::ParseTextWithEntities(_session, *data.vmessage())
					: TextWithEntities()),
				.date = data.vdate().v,
				.bidAmount = int64(data.vbid_amount().v),
				.round = data.vround().v,
				.number = data.vgift_num().value_or_empty(),
				.position = data.vpos().v,
				.nameHidden = data.is_name_hidden(),
			});
		}
		if (const auto entry = find(giftId)) {
			const auto count = int(gifts.size());
			if (entry->state.my.gotCount != count) {
				entry->state.my.gotCount = count;
				entry->changes.fire({});
			}
		}
		done(std::move(gifts));
	}).fail([=] {
		done({});
	}).send();
}

std::optional<Data::UniqueGiftAttributes> GiftAuctions::attributes(
		uint64 giftId) const {
	const auto i = _attributes.find(giftId);
	return (i != end(_attributes) && i->second.waiters.empty())
		? i->second.lists
		: std::optional<Data::UniqueGiftAttributes>();
}

void GiftAuctions::requestAttributes(uint64 giftId, Fn<void()> ready) {
	auto &entry = _attributes[giftId];
	entry.waiters.push_back(std::move(ready));
	if (entry.waiters.size() > 1) {
		return;
	}
	_session->api().request(MTPpayments_GetStarGiftUpgradeAttributes(
		MTP_long(giftId)
	)).done([=](const MTPpayments_StarGiftUpgradeAttributes &result) {
		const auto &attributes = result.data().vattributes().v;
		auto &entry = _attributes[giftId];
		auto &info = entry.lists;
		info.models.reserve(attributes.size());
		info.patterns.reserve(attributes.size());
		info.backdrops.reserve(attributes.size());
		for (const auto &attribute : attributes) {
			attribute.match([&](const MTPDstarGiftAttributeModel &data) {
				info.models.push_back(Api::FromTL(_session, data));
			}, [&](const MTPDstarGiftAttributePattern &data) {
				info.patterns.push_back(Api::FromTL(_session, data));
			}, [&](const MTPDstarGiftAttributeBackdrop &data) {
				info.backdrops.push_back(Api::FromTL(data));
			}, [](const MTPDstarGiftAttributeOriginalDetails &data) {
			});
		}
		for (const auto &ready : base::take(entry.waiters)) {
			ready();
		}
	}).fail([=] {
		for (const auto &ready : base::take(_attributes[giftId].waiters)) {
			ready();
		}
	}).send();
}

rpl::producer<ActiveAuctions> GiftAuctions::active() const {
	return _activeChanged.events_starting_with_copy(
		rpl::empty
	) | rpl::map([=] {
		return collectActive();
	});
}

rpl::producer<bool> GiftAuctions::hasActiveChanges() const {
	const auto has = hasActive();
	return _activeChanged.events(
	) | rpl::map([=] {
		return hasActive();
	}) | rpl::combine_previous(
		has
	) | rpl::filter([=](bool previous, bool current) {
		return previous != current;
	}) | rpl::map([=](bool previous, bool current) {
		return current;
	});
}

bool GiftAuctions::hasActive() const {
	for (const auto &[slug, entry] : _map) {
		if (myStateKey(entry->state)) {
			return true;
		}
	}
	return false;
}

void GiftAuctions::checkSubscriptions() {
	const auto now = crl::now();
	auto next = crl::time();
	for (const auto &[slug, entry] : _map) {
		const auto raw = entry.get();
		const auto till = raw->state.subscribedTill;
		if (till <= 0 || !raw->changes.has_consumers()) {
			continue;
		} else if (till <= now) {
			request(slug);
		} else {
			const auto timeout = till - now;
			if (!next || timeout < next) {
				next = timeout;
			}
		}
	}
	if (next) {
		_timer.callOnce(next);
	}
}

auto GiftAuctions::myStateKey(const GiftAuctionState &state) const
-> MyStateKey {
	if (!state.my.bid) {
		return {};
	}
	auto min = 0;
	for (const auto &level : state.bidLevels) {
		if (level.position > state.gift->auctionGiftsPerRound) {
			break;
		} else if (!min || min > level.amount) {
			min = level.amount;
		}
	}
	return {
		.bid = int(state.my.bid),
		.position = MyAuctionPosition(state),
		.version = state.version,
	};
}

ActiveAuctions GiftAuctions::collectActive() const {
	auto result = ActiveAuctions();
	result.list.reserve(_map.size());
	for (const auto &[slug, entry] : _map) {
		const auto raw = &entry->state;
		if (raw->gift && raw->my.date) {
			result.list.push_back(raw);
		}
	}
	return result;
}

uint64 GiftAuctions::countActiveHash() const {
	auto result = Api::HashInit();
	for (const auto &active : collectActive().list) {
		Api::HashUpdate(result, active->version);
		Api::HashUpdate(result, active->my.date);
	}
	return Api::HashFinalize(result);
}

void GiftAuctions::requestActive() {
	if (_activeRequestId) {
		return;
	}
	_activeRequestId = _session->api().request(
		MTPpayments_GetStarGiftActiveAuctions(MTP_long(countActiveHash()))
	).done([=](const MTPpayments_StarGiftActiveAuctions &result) {
		result.match([=](const MTPDpayments_starGiftActiveAuctions &data) {
			const auto owner = &_session->data();
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());

			auto giftsFound = base::flat_set<QString>();
			const auto &list = data.vauctions().v;
			giftsFound.reserve(list.size());
			for (const auto &auction : list) {
				const auto &data = auction.data();
				auto gift = Api::FromTL(_session, data.vgift());
				const auto slug = gift ? gift->auctionSlug : QString();
				if (slug.isEmpty()) {
					LOG(("Api Error: Bad auction gift."));
					continue;
				}
				auto &entry = _map[slug];
				if (!entry) {
					entry = std::make_unique<Entry>();
				}
				const auto raw = entry.get();
				if (!raw->state.gift) {
					raw->state.gift = std::move(gift);
				}
				apply(raw, data.vstate());
				apply(raw, data.vuser_state());
				giftsFound.emplace(slug);
			}
			for (const auto &[slug, entry] : _map) {
				const auto my = &entry->state.my;
				if (my->date && !giftsFound.contains(slug)) {
					my->to = nullptr;
					my->minBidAmount = 0;
					my->bid = 0;
					my->date = 0;
					my->returned = false;
					giftsFound.emplace(slug);
				}
			}
			for (const auto &slug : giftsFound) {
				_map[slug]->changes.fire({});
			}
			_activeChanged.fire({});
		}, [](const MTPDpayments_starGiftActiveAuctionsNotModified &) {
		});
	}).send();
}

void GiftAuctions::request(const QString &slug) {
	auto &entry = _map[slug];
	Assert(entry != nullptr);

	const auto raw = entry.get();
	if (raw->requested) {
		return;
	}
	raw->requested = true;
	_session->api().request(MTPpayments_GetStarGiftAuctionState(
		MTP_inputStarGiftAuctionSlug(MTP_string(slug)),
		MTP_int(raw->state.version)
	)).done([=](const MTPpayments_StarGiftAuctionState &result) {
		raw->requested = false;
		const auto &data = result.data();

		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		raw->state.gift = Api::FromTL(_session, data.vgift());
		if (!raw->state.gift) {
			return;
		}
		const auto timeout = data.vtimeout().v;
		const auto ms = timeout * crl::time(1000);
		raw->state.subscribedTill = ms ? (crl::now() + ms) : -1;

		const auto was = myStateKey(raw->state);
		apply(raw, data.vstate());
		apply(raw, data.vuser_state());
		if (raw->changes.has_consumers()) {
			raw->changes.fire({});
			if (ms && (!_timer.isActive() || _timer.remainingTime() > ms)) {
				_timer.callOnce(ms);
			}
		}
		if (was != myStateKey(raw->state)) {
			_activeChanged.fire({});
		}
	}).send();
}

GiftAuctions::Entry *GiftAuctions::find(uint64 giftId) const {
	for (const auto &[slug, entry] : _map) {
		if (entry->state.gift && entry->state.gift->id == giftId) {
			return entry.get();
		}
	}
	return nullptr;
}

void GiftAuctions::apply(
		not_null<Entry*> entry,
		const MTPStarGiftAuctionState &state) {
	apply(&entry->state, state);
}

void GiftAuctions::apply(
		not_null<GiftAuctionState*> entry,
		const MTPStarGiftAuctionState &state) {
	Expects(entry->gift.has_value());

	state.match([&](const MTPDstarGiftAuctionState &data) {
		const auto version = data.vversion().v;
		if (entry->version >= version) {
			return;
		}
		const auto owner = &_session->data();
		entry->startDate = data.vstart_date().v;
		entry->endDate = data.vend_date().v;
		entry->minBidAmount = data.vmin_bid_amount().v;
		const auto &levels = data.vbid_levels().v;
		entry->bidLevels.clear();
		entry->bidLevels.reserve(levels.size());
		for (const auto &level : levels) {
			auto &bid = entry->bidLevels.emplace_back();
			const auto &data = level.data();
			bid.amount = data.vamount().v;
			bid.position = data.vpos().v;
			bid.date = data.vdate().v;
		}
		const auto &top = data.vtop_bidders().v;
		entry->topBidders.clear();
		entry->topBidders.reserve(top.size());
		for (const auto &user : top) {
			entry->topBidders.push_back(owner->user(UserId(user.v)));
		}
		entry->nextRoundAt = data.vnext_round_at().v;
		entry->giftsLeft = data.vgifts_left().v;
		entry->currentRound = data.vcurrent_round().v;
		entry->totalRounds = data.vtotal_rounds().v;
		const auto &rounds = data.vrounds().v;
		entry->roundParameters.clear();
		entry->roundParameters.reserve(rounds.size());
		for (const auto &round : rounds) {
			round.match([&](const MTPDstarGiftAuctionRound &data) {
				entry->roundParameters.push_back({
					.number = data.vnum().v,
					.duration = data.vduration().v,
				});
			}, [&](const MTPDstarGiftAuctionRoundExtendable &data) {
				entry->roundParameters.push_back({
					.number = data.vnum().v,
					.duration = data.vduration().v,
					.extendTop = data.vextend_top().v,
					.extendDuration = data.vextend_window().v,
				});
			});
		}
		entry->averagePrice = 0;
	}, [&](const MTPDstarGiftAuctionStateFinished &data) {
		entry->averagePrice = data.vaverage_price().v;
		entry->startDate = data.vstart_date().v;
		entry->endDate = data.vend_date().v;
		entry->minBidAmount = 0;
		entry->nextRoundAt
			= entry->currentRound
			= entry->totalRounds
			= entry->giftsLeft
			= entry->version
			= 0;
	}, [&](const MTPDstarGiftAuctionStateNotModified &data) {
	});
}

void GiftAuctions::apply(
		not_null<Entry*> entry,
		const MTPStarGiftAuctionUserState &state) {
	apply(&entry->state.my, state);
}

void GiftAuctions::apply(
		not_null<StarGiftAuctionMyState*> entry,
		const MTPStarGiftAuctionUserState &state) {
	const auto &data = state.data();
	entry->to = data.vbid_peer()
		? _session->data().peer(peerFromMTP(*data.vbid_peer())).get()
		: nullptr;
	entry->minBidAmount = data.vmin_bid_amount().value_or(0);
	entry->bid = data.vbid_amount().value_or(0);
	entry->date = data.vbid_date().value_or(0);
	entry->gotCount = data.vacquired_count().v;
	entry->returned = data.is_returned();
}

int MyAuctionPosition(const GiftAuctionState &state) {
	const auto &levels = state.bidLevels;
	for (auto i = begin(levels), e = end(levels); i != e; ++i) {
		if (i->amount < state.my.bid
			|| (i->amount == state.my.bid && i->date >= state.my.date)) {
			return i->position;
		}
	}
	return (levels.empty() ? 0 : levels.back().position) + 1;
}

} // namespace Data
