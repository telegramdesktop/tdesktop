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

	const auto result = _local->start(passcode);
	if (result == Storage::StartResult::Success) {
		if (Local::oldSettingsVersion() < AppVersion) {
			Local::writeSettings();
		}
		activateAfterStarting();
	} else {
		Assert(!started());
	}
	return result;
}

void Accounts::accountAddedInStorage(
		int index,
		std::unique_ptr<Account> account) {
	Expects(account != nullptr);
	Expects(!_accounts.contains(index));

	if (_accounts.empty()) {
		_activeIndex = index;
	}
	_accounts.emplace(index, std::move(account));
};

void Accounts::resetWithForgottenPasscode() {
	if (_accounts.empty()) {
		_local->startFromScratch();
		activateAfterStarting();
	} else {
		for (const auto &[index, account] : _accounts) {
			account->logOut();
		}
	}
}

void Accounts::activateAfterStarting() {
	Expects(started());

	for (const auto &[index, account] : _accounts) {
		watchSession(account.get());
	}

	activate(_activeIndex);
}

const base::flat_map<int, std::unique_ptr<Account>> &Accounts::list() const {
	return _accounts;
}

rpl::producer<Account*> Accounts::activeValue() const {
	return _active.value();
}

int Accounts::activeIndex() const {
	Expects(_accounts.contains(_activeIndex));

	return _activeIndex;
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
	const auto account = _accounts.emplace(
		index,
		std::make_unique<Account>(_dataName, index)
	).first->second.get();
	_local->startAdded(account);
	watchSession(account);
	return index;
}

void Accounts::watchSession(not_null<Account*> account) {
	account->startMtp();
	account->sessionChanges(
	) | rpl::filter([=](Session *session) {
		return !session && _accounts.size() > 1;
	}) | rpl::start_with_next([=](Session *session) {
		if (account == _active.current()) {
			activateAuthedAccount();
		}
		crl::on_main(&Core::App(), [=] {
			removeRedundantAccounts();
		});
	}, account->lifetime());
}

void Accounts::activateAuthedAccount() {
	Expects(started());

	if (_active.current()->sessionExists()) {
		return;
	}
	for (auto i = _accounts.begin(); i != _accounts.end(); ++i) {
		if (i->second->sessionExists()) {
			activate(i->first);
			return;
		}
	}
}

void Accounts::removeRedundantAccounts() {
	Expects(started());

	const auto was = _accounts.size();
	activateAuthedAccount();
	for (auto i = _accounts.begin(); i != _accounts.end();) {
		if (i->second.get() == _active.current()
			|| i->second->sessionExists()) {
			++i;
			continue;
		}
		i = _accounts.erase(i);
	}
	if (_accounts.size() != was) {
		scheduleWriteAccounts();
	}
}

void Accounts::activate(int index) {
	Expects(_accounts.contains(index));

	_activeLifetime.destroy();
	_activeIndex = index;
	_active = _accounts.find(index)->second.get();
	_active.current()->sessionValue(
	) | rpl::start_to_stream(_activeSessions, _activeLifetime);

	scheduleWriteAccounts();
}

void Accounts::scheduleWriteAccounts() {
	if (_writeAccountsScheduled) {
		return;
	}
	_writeAccountsScheduled = true;
	crl::on_main(&Core::App(), [=] {
		_writeAccountsScheduled = false;
		_local->writeAccounts();
	});
}

} // namespace Main
