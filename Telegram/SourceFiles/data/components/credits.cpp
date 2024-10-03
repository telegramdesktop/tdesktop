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
	apply(data.vbalance().v);
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

uint64 Credits::balance() const {
	return _nonLockedBalance.current();
}

uint64 Credits::balance(PeerId peerId) const {
	const auto it = _cachedPeerBalances.find(peerId);
	return (it != _cachedPeerBalances.end()) ? it->second : 0;
}

rpl::producer<uint64> Credits::balanceValue() const {
	return _nonLockedBalance.value();
}

void Credits::updateNonLockedValue() {
	_nonLockedBalance = (_balance >= _locked) ? (_balance - _locked) : 0;
}

void Credits::lock(int count) {
	Expects(loaded());
	Expects(count >= 0);
	Expects(_locked + count <= _balance);

	_locked += count;

	updateNonLockedValue();
}

void Credits::unlock(int count) {
	Expects(count >= 0);
	Expects(_locked >= count);

	_locked -= count;

	updateNonLockedValue();
}

void Credits::withdrawLocked(int count) {
	Expects(count >= 0);
	Expects(_locked >= count);

	_locked -= count;
	apply(_balance >= count ? (_balance - count) : 0);
	invalidate();
}

void Credits::invalidate() {
	_reload.call();
}

void Credits::apply(uint64 balance) {
	_balance = balance;
	updateNonLockedValue();

	const auto was = std::exchange(_lastLoaded, crl::now());
	if (!was) {
		_loadedChanges.fire({});
	}
}

void Credits::apply(PeerId peerId, uint64 balance) {
	_cachedPeerBalances[peerId] = balance;
}

} // namespace Data
