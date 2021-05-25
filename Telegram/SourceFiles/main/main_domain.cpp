/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_domain.h"

#include "core/application.h"
#include "core/shortcuts.h"
#include "core/crash_reports.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "storage/storage_domain.h"
#include "storage/storage_account.h"
#include "storage/localstorage.h"
#include "export/export_settings.h"
#include "window/notifications_manager.h"
#include "facades.h"

namespace Main {

Domain::Domain(const QString &dataName)
: _dataName(dataName)
, _local(std::make_unique<Storage::Domain>(this, dataName)) {
	_active.changes(
	) | rpl::take(1) | rpl::start_with_next([] {
		// In case we had a legacy passcoded app we start settings here.
		Core::App().startSettingsAndBackground();

		Core::App().notifications().createManager();
	}, _lifetime);

	_active.changes(
	) | rpl::map([](Main::Account *account) {
		return account ? account->sessionValue() : rpl::never<Session*>();
		}) | rpl::flatten_latest(
	) | rpl::map([](Main::Session *session) {
		return session
			? session->changes().peerFlagsValue(
				session->user(),
				Data::PeerUpdate::Flag::Username)
			: rpl::never<Data::PeerUpdate>();
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([](const Data::PeerUpdate &update) {
		CrashReports::SetAnnotation("Username", update.peer->userName());
	}, _lifetime);
}

Domain::~Domain() = default;

bool Domain::started() const {
	return !_accounts.empty();
}

Storage::StartResult Domain::start(const QByteArray &passcode) {
	Expects(!started());

	const auto result = _local->start(passcode);
	if (result == Storage::StartResult::Success) {
		activateAfterStarting();
		crl::on_main(&Core::App(), [=] { suggestExportIfNeeded(); });
	} else {
		Assert(!started());
	}
	return result;
}

void Domain::finish() {
	_accountToActivate = -1;
	_active = nullptr;
	base::take(_accounts);
}

void Domain::suggestExportIfNeeded() {
	Expects(started());

	for (const auto &[index, account] : _accounts) {
		if (const auto session = account->maybeSession()) {
			const auto settings = session->local().readExportSettings();
			if (const auto availableAt = settings.availableAt) {
				session->data().suggestStartExport(availableAt);
			}
		}
	}
}

void Domain::accountAddedInStorage(AccountWithIndex accountWithIndex) {
	Expects(accountWithIndex.account != nullptr);

	for (const auto &[index, _] : _accounts) {
		if (index == accountWithIndex.index) {
			Unexpected("Repeated account index.");
		}
	}
	_accounts.push_back(std::move(accountWithIndex));
}

void Domain::activateFromStorage(int index) {
	_accountToActivate = index;
}

int Domain::activeForStorage() const {
	return _accountToActivate;
}

void Domain::resetWithForgottenPasscode() {
	if (_accounts.empty()) {
		_local->startFromScratch();
		activateAfterStarting();
	} else {
		for (const auto &[index, account] : _accounts) {
			account->logOut();
		}
	}
}

void Domain::activateAfterStarting() {
	Expects(started());

	auto toActivate = _accounts.front().account.get();
	for (const auto &[index, account] : _accounts) {
		if (index == _accountToActivate) {
			toActivate = account.get();
		}
		watchSession(account.get());
	}

	activate(toActivate);
	removePasscodeIfEmpty();
}

const std::vector<Domain::AccountWithIndex> &Domain::accounts() const {
	return _accounts;
}

rpl::producer<> Domain::accountsChanges() const {
	return _accountsChanges.events();
}

Account *Domain::maybeLastOrSomeAuthedAccount() {
	auto result = (Account*)nullptr;
	for (const auto &[index, account] : _accounts) {
		if (!account->sessionExists()) {
			continue;
		} else if (index == _lastActiveIndex) {
			return account.get();
		} else if (!result) {
			result = account.get();
		}
	}
	return result;
}

rpl::producer<Account*> Domain::activeValue() const {
	return _active.value();
}

Account &Domain::active() const {
	Expects(!_accounts.empty());

	Ensures(_active.current() != nullptr);
	return *_active.current();
}



rpl::producer<not_null<Account*>> Domain::activeChanges() const {
	return _active.changes() | rpl::map([](Account *value) {
		return not_null{ value };
	});
}

rpl::producer<Session*> Domain::activeSessionChanges() const {
	return _activeSessions.events();
}

rpl::producer<Session*> Domain::activeSessionValue() const {
	const auto current = _accounts.empty()
		? nullptr
		: active().maybeSession();
	return rpl::single(current) | rpl::then(_activeSessions.events());
}

int Domain::unreadBadge() const {
	return _unreadBadge;
}

bool Domain::unreadBadgeMuted() const {
	return _unreadBadgeMuted;
}

rpl::producer<> Domain::unreadBadgeChanges() const {
	return _unreadBadgeChanges.events();
}

void Domain::notifyUnreadBadgeChanged() {
	for (const auto &[index, account] : _accounts) {
		if (const auto session = account->maybeSession()) {
			session->data().notifyUnreadBadgeChanged();
		}
	}
}

void Domain::updateUnreadBadge() {
	_unreadBadge = 0;
	_unreadBadgeMuted = true;
	for (const auto &[index, account] : _accounts) {
		if (const auto session = account->maybeSession()) {
			const auto data = &session->data();
			_unreadBadge += data->unreadBadge();
			if (!data->unreadBadgeMuted()) {
				_unreadBadgeMuted = false;
			}
		}
	}
	_unreadBadgeChanges.fire({});
}

void Domain::scheduleUpdateUnreadBadge() {
	if (_unreadBadgeUpdateScheduled) {
		return;
	}
	_unreadBadgeUpdateScheduled = true;
	Core::App().postponeCall(crl::guard(&Core::App(), [=] {
		_unreadBadgeUpdateScheduled = false;
		updateUnreadBadge();
	}));
}

not_null<Main::Account*> Domain::add(MTP::Environment environment) {
	Expects(started());
	Expects(_accounts.size() < kMaxAccounts);

	static const auto cloneConfig = [](const MTP::Config &config) {
		return std::make_unique<MTP::Config>(config);
	};
	auto mainDcId = MTP::Instance::Fields::kNotSetMainDc;
	const auto accountConfig = [&](not_null<Account*> account) {
		mainDcId = account->mtp().mainDcId();
		return cloneConfig(account->mtp().config());
	};
	auto config = [&] {
		if (_active.current()->mtp().environment() == environment) {
			return accountConfig(_active.current());
		}
		for (const auto &[index, account] : _accounts) {
			if (account->mtp().environment() == environment) {
				return accountConfig(account.get());
			}
		}
		return (environment == MTP::Environment::Production)
			? cloneConfig(Core::App().fallbackProductionConfig())
			: std::make_unique<MTP::Config>(environment);
	}();
	auto index = 0;
	while (ranges::contains(_accounts, index, &AccountWithIndex::index)) {
		++index;
	}
	_accounts.push_back(AccountWithIndex{
		.index = index,
		.account = std::make_unique<Account>(this, _dataName, index)
	});
	const auto account = _accounts.back().account.get();
	account->setMtpMainDcId(mainDcId);
	_local->startAdded(account, std::move(config));
	watchSession(account);
	_accountsChanges.fire({});

	auto &settings = Core::App().settings();
	if (_accounts.size() == 2 && !settings.mainMenuAccountsShown()) {
		settings.setMainMenuAccountsShown(true);
		Core::App().saveSettingsDelayed();
	}

	return account;
}

void Domain::addActivated(MTP::Environment environment) {
	if (accounts().size() < Main::Domain::kMaxAccounts) {
		activate(add(environment));
	} else {
		for (auto &[index, account] : accounts()) {
			if (!account->sessionExists()
				&& account->mtp().environment() == environment) {
				activate(account.get());
				break;
			}
		}
	}
}

void Domain::watchSession(not_null<Account*> account) {
	account->sessionValue(
	) | rpl::filter([=](Session *session) {
		return session != nullptr;
	}) | rpl::start_with_next([=](Session *session) {
		session->data().unreadBadgeChanges(
		) | rpl::start_with_next([=] {
			scheduleUpdateUnreadBadge();
		}, session->lifetime());
	}, account->lifetime());

	account->sessionChanges(
	) | rpl::filter([=](Session *session) {
		return !session;
	}) | rpl::start_with_next([=] {
		scheduleUpdateUnreadBadge();
		if (account == _active.current()) {
			activateAuthedAccount();
		}
		crl::on_main(&Core::App(), [=] {
			removeRedundantAccounts();
		});
	}, account->lifetime());
}

void Domain::activateAuthedAccount() {
	Expects(started());

	if (_active.current()->sessionExists()) {
		return;
	}
	for (auto i = _accounts.begin(); i != _accounts.end(); ++i) {
		if (i->account->sessionExists()) {
			activate(i->account.get());
			return;
		}
	}
}

bool Domain::removePasscodeIfEmpty() {
	if (_accounts.size() != 1 || _active.current()->sessionExists()) {
		return false;
	}
	Local::reset();
	if (!_local->hasLocalPasscode()) {
		return false;
	}
	// We completely logged out, remove the passcode if it was there.
	Core::App().unlockPasscode();
	_local->setPasscode(QByteArray());
	return true;
}

void Domain::removeRedundantAccounts() {
	Expects(started());

	const auto was = _accounts.size();
	activateAuthedAccount();
	for (auto i = _accounts.begin(); i != _accounts.end();) {
		if (i->account.get() == _active.current()
			|| i->account->sessionExists()) {
			++i;
			continue;
		}
		checkForLastProductionConfig(i->account.get());
		i = _accounts.erase(i);
	}

	if (!removePasscodeIfEmpty() && _accounts.size() != was) {
		scheduleWriteAccounts();
		_accountsChanges.fire({});
	}
}

void Domain::checkForLastProductionConfig(
		not_null<Main::Account*> account) {
	const auto mtp = &account->mtp();
	if (mtp->environment() != MTP::Environment::Production) {
		return;
	}
	for (const auto &[index, other] : _accounts) {
		if (other.get() != account
			&& other->mtp().environment() == MTP::Environment::Production) {
			return;
		}
	}
	Core::App().refreshFallbackProductionConfig(mtp->config());
}

void Domain::maybeActivate(not_null<Main::Account*> account) {
	Core::App().preventOrInvoke(crl::guard(account, [=] {
		activate(account);
	}));
}

void Domain::activate(not_null<Main::Account*> account) {
	if (_active.current() == account.get()) {
		return;
	}
	const auto i = ranges::find(_accounts, account.get(), [](
			const AccountWithIndex &value) {
		return value.account.get();
	});
	Assert(i != end(_accounts));
	const auto changed = (_accountToActivate != i->index);
	auto wasAuthed = false;

	_activeLifetime.destroy();
	if (_active.current()) {
		_lastActiveIndex = _accountToActivate;
		wasAuthed = _active.current()->sessionExists();
	}
	_accountToActivate = i->index;
	_active = account.get();
	_active.current()->sessionValue(
	) | rpl::start_to_stream(_activeSessions, _activeLifetime);

	if (changed) {
		if (wasAuthed) {
			scheduleWriteAccounts();
		} else {
			crl::on_main(&Core::App(), [=] {
				removeRedundantAccounts();
			});
		}
	}
}

void Domain::scheduleWriteAccounts() {
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
