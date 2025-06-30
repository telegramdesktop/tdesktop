/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/credits.h"

#include "apiwrap.h"
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

float64 Credits::usdRate() const {
	return _session->appConfig().currencyWithdrawRate();
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

CreditsAmount Credits::balanceCurrency(PeerId peerId) const {
	const auto it = _cachedPeerCurrencyBalances.find(peerId);
	return (it != _cachedPeerCurrencyBalances.end())
		? it->second
		: CreditsAmount(0, 0, CreditsType::Ton);
}

rpl::producer<CreditsAmount> Credits::balanceValue() const {
	return _nonLockedBalance.value();
}

void Credits::tonLoad(bool force) {
	if (_tonRequestId
		|| (!force
			&& _tonLastLoaded
			&& _tonLastLoaded + kReloadThreshold > crl::now())) {
		return;
	}
	_tonRequestId = _session->api().request(MTPpayments_GetStarsStatus(
		MTP_flags(MTPpayments_GetStarsStatus::Flag::f_ton),
		MTP_inputPeerSelf()
	)).done([=](const MTPpayments_StarsStatus &result) {
		_tonRequestId = 0;
		const auto amount = CreditsAmountFromTL(result.data().vbalance());
		if (amount.ton()) {
			apply(amount);
		} else if (amount.empty()) {
			apply(CreditsAmount(0, CreditsType::Ton));
		} else {
			LOG(("API Error: Got weird balance."));
		}
	}).fail([=](const MTP::Error &error) {
		_tonRequestId = 0;
		LOG(("API Error: Couldn't get TON balance, error: %1"
			).arg(error.type()));
	}).send();
}

bool Credits::tonLoaded() const {
	return _tonLastLoaded != 0;
}

rpl::producer<bool> Credits::tonLoadedValue() const {
	if (tonLoaded()) {
		return rpl::single(true);
	}
	return rpl::single(
		false
	) | rpl::then(_tonLoadedChanges.events() | rpl::map_to(true));
}

CreditsAmount Credits::tonBalance() const {
	return _tonBalance.current();
}

rpl::producer<CreditsAmount> Credits::tonBalanceValue() const {
	return _tonBalance.value();
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
	if (balance.ton()) {
		_tonBalance = balance;

		const auto was = std::exchange(_tonLastLoaded, crl::now());
		if (!was) {
			_tonLoadedChanges.fire({});
		}
	} else {
		_balance = balance;
		updateNonLockedValue();

		const auto was = std::exchange(_lastLoaded, crl::now());
		if (!was) {
			_loadedChanges.fire({});
		}
	}
}

void Credits::apply(PeerId peerId, CreditsAmount balance) {
	_cachedPeerBalances[peerId] = balance;
	_refreshedByPeerId.fire_copy(peerId);
}

void Credits::applyCurrency(PeerId peerId, CreditsAmount balance) {
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
		const auto isNegative = (static_cast<int64_t>(data.vamount().v) < 0);
		const auto absValue = isNegative
			? uint64(~data.vamount().v + 1)
			: data.vamount().v;
		const auto result = CreditsAmount(
			int64(absValue / 1'000'000'000),
			absValue % 1'000'000'000,
			CreditsType::Ton);
		return isNegative
			? CreditsAmount(0, CreditsType::Ton) - result
			: result;
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
