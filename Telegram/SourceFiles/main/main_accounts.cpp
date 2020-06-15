/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_accounts.h"

#include "core/application.h"
#include "core/shortcuts.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/storage_accounts.h"
#include "storage/localstorage.h"

namespace Main {

Accounts::Accounts(const QString &dataName)
: _dataName(dataName)
, _local(std::make_unique<Storage::Accounts>(this, dataName)) {
}

Accounts::~Accounts() = default;

bool Accounts::started() const {
	return !_accounts.empty();
}

Storage::StartResult Accounts::start(const QByteArray &passcode) {
	Expects(!started());

	auto active = -1;
	const auto callback = [&](int index, std::unique_ptr<Account> account) {
		Expects(account != nullptr);
		Expects(!_accounts.contains(index));

		if (_accounts.empty()) {
			active = index;
		}
		_accounts.emplace(index, std::move(account));
	};
	const auto result = _local->start(passcode, callback);
	if (result == Storage::StartResult::Success) {
		Assert(started());

		activate(active);

		for (const auto &[index, account] : _accounts) {
			account->startMtp();
		}
		if (Local::oldSettingsVersion() < AppVersion) {
			Local::writeSettings();
		}
	} else {
		Assert(!started());
	}
	return result;
}

const base::flat_map<int, std::unique_ptr<Account>> &Accounts::list() const {
	return _accounts;
}

rpl::producer<Account*> Accounts::activeValue() const {
	return _active.value();
}

Account &Accounts::active() const {
	Expects(!_accounts.empty());

	Ensures(_active.current() != nullptr);
	return *_active.current();
}

rpl::producer<not_null<Account*>> Accounts::activeChanges() const {
	return _active.changes() | rpl::map([](Account *value) {
		return not_null{ value };
	});
}

rpl::producer<Session*> Accounts::activeSessionChanges() const {
	return _activeSessions.events();
}

rpl::producer<Session*> Accounts::activeSessionValue() const {
	const auto current = (_accounts.empty() || !active().sessionExists())
		? nullptr
		: &active().session();
	return rpl::single(current) | rpl::then(_activeSessions.events());
}

int Accounts::add() {
	auto index = 0;
	while (_accounts.contains(index)) {
		++index;
	}
	_accounts.emplace(index, std::make_unique<Account>(_dataName, index));
	return index;
}

void Accounts::activate(int index) {
	Expects(_accounts.contains(index));

	_activeLifetime.destroy();
	_activeIndex = index;
	_active = _accounts.find(index)->second.get();
	_active.current()->sessionValue(
	) | rpl::start_to_stream(_activeSessions, _activeLifetime);
}

} // namespace Main
