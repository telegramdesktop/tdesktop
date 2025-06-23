/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/credits.h"

#include "api/api_credits.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kReloadThreshold = 60 * crl::time(1000);

} // namespace

Credits::Credits(not_null<Main::Session*> session)
: _session(session)
, _reload([=] { load(true); }) {
}

Credits::~Credits() = default;

void Credits::apply(const MTPDupdateStarsBalance &data) {
	apply(CreditsAmountFromTL(data.vbalance()));
}

rpl::producer<float64> Credits::rateValue(
		not_null<PeerData*> ownedBotOrChannel) {
	return rpl::single(_session->appConfig().starsWithdrawRate());
}

void Credits::load(bool force) {
	if (_loader
		|| (!force
			&& _lastLoaded
			&& _lastLoaded + kReloadThreshold > crl::now())) {
		return;
	}
	const auto self = _session->user();
	_loader = std::make_unique<rpl::lifetime>();
	_loader->make_state<Api::CreditsStatus>(self)->request({}, [=](
			Data::CreditsStatusSlice slice) {
		const auto balance = slice.balance;
		const auto apiStats
			= _loader->make_state<Api::CreditsEarnStatistics>(self);
		const auto finish = [=](bool statsEnabled) {
			_statsEnabled = statsEnabled;
			apply(balance);
			_loader = nullptr;
		};
		apiStats->request() | rpl::start_with_error_done([=] {
			finish(false);
		}, [=] {
			finish(true);
		}, *_loader);
	});
}

bool Credits::loaded() const {
	return _lastLoaded != 0;
}

rpl::producer<bool> Credits::loadedValue() const {
	if (loaded()) {
		return rpl::single(true);
	}
	return rpl::single(
		false
	) | rpl::then(_loadedChanges.events() | rpl::map_to(true));
}

CreditsAmount Credits::balance() const {
	return _nonLockedBalance.current();
}

CreditsAmount Credits::balance(PeerId peerId) const {
	const auto it = _cachedPeerBalances.find(peerId);
	return (it != _cachedPeerBalances.end()) ? it->second : CreditsAmount();
}

uint64 Credits::balanceCurrency(PeerId peerId) const {
	const auto it = _cachedPeerCurrencyBalances.find(peerId);
	return (it != _cachedPeerCurrencyBalances.end()) ? it->second : 0;
}

rpl::producer<CreditsAmount> Credits::balanceValue() const {
	return _nonLockedBalance.value();
}

void Credits::updateNonLockedValue() {
	_nonLockedBalance = (_balance >= _locked)
		? (_balance - _locked)
		: CreditsAmount();
}

void Credits::lock(CreditsAmount count) {
	Expects(loaded());
	Expects(count >= CreditsAmount(0));
	Expects(_locked + count <= _balance);

	_locked += count;

	updateNonLockedValue();
}

void Credits::unlock(CreditsAmount count) {
	Expects(count >= CreditsAmount(0));
	Expects(_locked >= count);

	_locked -= count;

	updateNonLockedValue();
}

void Credits::withdrawLocked(CreditsAmount count) {
	Expects(count >= CreditsAmount(0));
	Expects(_locked >= count);

	_locked -= count;
	apply(_balance >= count ? (_balance - count) : CreditsAmount(0));
	invalidate();
}

void Credits::invalidate() {
	_reload.call();
}

void Credits::apply(CreditsAmount balance) {
	_balance = balance;
	updateNonLockedValue();

	const auto was = std::exchange(_lastLoaded, crl::now());
	if (!was) {
		_loadedChanges.fire({});
	}
}

void Credits::apply(PeerId peerId, CreditsAmount balance) {
	_cachedPeerBalances[peerId] = balance;
	_refreshedByPeerId.fire_copy(peerId);
}

void Credits::applyCurrency(PeerId peerId, uint64 balance) {
	_cachedPeerCurrencyBalances[peerId] = balance;
	_refreshedByPeerId.fire_copy(peerId);
}

rpl::producer<> Credits::refreshedByPeerId(PeerId peerId) {
	return _refreshedByPeerId.events(
	) | rpl::filter(rpl::mappers::_1 == peerId) | rpl::to_empty;
}

bool Credits::statsEnabled() const {
	return _statsEnabled;
}

} // namespace Data

CreditsAmount CreditsAmountFromTL(const MTPStarsAmount &amount) {
	return amount.match([&](const MTPDstarsAmount &data) {
		return CreditsAmount(
			data.vamount().v,
			data.vnanos().v,
			CreditsType::Stars);
	}, [&](const MTPDstarsTonAmount &data) {
		return CreditsAmount(
			data.vamount().v / uint64(1'000'000'000),
			data.vamount().v % uint64(1'000'000'000),
			CreditsType::Ton);
	});
}

CreditsAmount CreditsAmountFromTL(const MTPStarsAmount *amount) {
	return amount ? CreditsAmountFromTL(*amount) : CreditsAmount();
}

MTPStarsAmount StarsAmountToTL(CreditsAmount amount) {
	return amount.ton() ? MTP_starsTonAmount(
		MTP_long(amount.whole() * uint64(1'000'000'000) + amount.nano())
	) : MTP_starsAmount(MTP_long(amount.whole()), MTP_int(amount.nano()));
}
