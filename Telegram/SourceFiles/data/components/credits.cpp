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

StarsAmount FromTL(const MTPStarsAmount &value) {
	const auto &data = value.data();
	return StarsAmount(data.vamount().v, data.vnanos().v);
}

Credits::Credits(not_null<Main::Session*> session)
: _session(session)
, _reload([=] { load(true); }) {
}

Credits::~Credits() = default;

void Credits::apply(const MTPDupdateStarsBalance &data) {
	apply(FromTL(data.vbalance()));
}

rpl::producer<float64> Credits::rateValue(
		not_null<PeerData*> ownedBotOrChannel) {
	return rpl::single(
		_session->appConfig().get<float64>(
			u"stars_usd_withdraw_rate_x1000"_q,
			1200) / 1000.);
}

void Credits::load(bool force) {
	if (_loader
		|| (!force
			&& _lastLoaded
			&& _lastLoaded + kReloadThreshold > crl::now())) {
		return;
	}
	_loader = std::make_unique<Api::CreditsStatus>(_session->user());
	_loader->request({}, [=](Data::CreditsStatusSlice slice) {
		_loader = nullptr;
		apply(slice.balance);
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

StarsAmount Credits::balance() const {
	return _nonLockedBalance.current();
}

StarsAmount Credits::balance(PeerId peerId) const {
	const auto it = _cachedPeerBalances.find(peerId);
	return (it != _cachedPeerBalances.end()) ? it->second : StarsAmount();
}

uint64 Credits::balanceCurrency(PeerId peerId) const {
	const auto it = _cachedPeerCurrencyBalances.find(peerId);
	return (it != _cachedPeerCurrencyBalances.end()) ? it->second : 0;
}

rpl::producer<StarsAmount> Credits::balanceValue() const {
	return _nonLockedBalance.value();
}

void Credits::updateNonLockedValue() {
	_nonLockedBalance = (_balance >= _locked)
		? (_balance - _locked)
		: StarsAmount();
}

void Credits::lock(StarsAmount count) {
	Expects(loaded());
	Expects(count >= StarsAmount(0));
	Expects(_locked + count <= _balance);

	_locked += count;

	updateNonLockedValue();
}

void Credits::unlock(StarsAmount count) {
	Expects(count >= StarsAmount(0));
	Expects(_locked >= count);

	_locked -= count;

	updateNonLockedValue();
}

void Credits::withdrawLocked(StarsAmount count) {
	Expects(count >= StarsAmount(0));
	Expects(_locked >= count);

	_locked -= count;
	apply(_balance >= count ? (_balance - count) : StarsAmount(0));
	invalidate();
}

void Credits::invalidate() {
	_reload.call();
}

void Credits::apply(StarsAmount balance) {
	_balance = balance;
	updateNonLockedValue();

	const auto was = std::exchange(_lastLoaded, crl::now());
	if (!was) {
		_loadedChanges.fire({});
	}
}

void Credits::apply(PeerId peerId, StarsAmount balance) {
	_cachedPeerBalances[peerId] = balance;
}

void Credits::applyCurrency(PeerId peerId, uint64 balance) {
	_cachedPeerCurrencyBalances[peerId] = balance;
}

} // namespace Data
