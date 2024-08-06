/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/credits.h"

#include "api/api_credits.h"
#include "data/data_user.h"
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
	const auto balance = _balance.current();
	const auto locked = _locked.current();
	return (balance >= locked) ? (balance - locked) : 0;
}

rpl::producer<uint64> Credits::balanceValue() const {
	return rpl::combine(
		_balance.value(),
		_locked.value()
	) | rpl::map([=](uint64 balance, uint64 locked) {
		return (balance >= locked) ? (balance - locked) : 0;
	});
}

void Credits::lock(int count) {
	Expects(loaded());
	Expects(count >= 0);

	_locked = _locked.current() + count;

	Ensures(_locked.current() <= _balance.current());
}

void Credits::unlock(int count) {
	Expects(count >= 0);
	Expects(_locked.current() >= count);

	_locked = _locked.current() - count;
}

void Credits::withdrawLocked(int count) {
	Expects(count >= 0);
	Expects(_locked.current() >= count);

	const auto balance = _balance.current();
	_locked = _locked.current() - count;
	apply(balance >= count ? (balance - count) : 0);
	invalidate();
}

void Credits::invalidate() {
	_reload.call();
}

void Credits::apply(uint64 balance) {
	_balance = balance;

	const auto was = std::exchange(_lastLoaded, crl::now());
	if (!was) {
		_loadedChanges.fire({});
	}
}

} // namespace Data
