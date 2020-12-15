/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Storage {
class Domain;
enum class StartResult : uchar;
} // namespace Storage

namespace MTP {
enum class Environment : uchar;
} // namespace MTP

namespace Main {

class Account;
class Session;

class Domain final {
public:
	struct AccountWithIndex {
		int index = 0;
		std::unique_ptr<Account> account;
	};

	static constexpr auto kMaxAccounts = 100;

	explicit Domain(const QString &dataName);
	~Domain();

	[[nodiscard]] bool started() const;
	[[nodiscard]] Storage::StartResult start(const QByteArray &passcode);
	void resetWithForgottenPasscode();
	void finish();

	[[nodiscard]] Storage::Domain &local() const {
		return *_local;
	}

	[[nodiscard]] auto accounts() const
		-> const std::vector<AccountWithIndex> &;
	[[nodiscard]] rpl::producer<Account*> activeValue() const;
	[[nodiscard]] rpl::producer<> accountsChanges() const;
	[[nodiscard]] Account *maybeLastOrSomeAuthedAccount();

	// Expects(started());
	[[nodiscard]] Account &active() const;
	[[nodiscard]] rpl::producer<not_null<Account*>> activeChanges() const;

	[[nodiscard]] rpl::producer<Session*> activeSessionValue() const;
	[[nodiscard]] rpl::producer<Session*> activeSessionChanges() const;

	[[nodiscard]] int unreadBadge() const;
	[[nodiscard]] bool unreadBadgeMuted() const;
	[[nodiscard]] rpl::producer<> unreadBadgeChanges() const;
	void notifyUnreadBadgeChanged();

	[[nodiscard]] not_null<Main::Account*> add(MTP::Environment environment);
	void maybeActivate(not_null<Main::Account*> account);
	void activate(not_null<Main::Account*> account);
	void addActivated(MTP::Environment environment);

	// Interface for Storage::Domain.
	void accountAddedInStorage(AccountWithIndex accountWithIndex);
	void activateFromStorage(int index);
	[[nodiscard]] int activeForStorage() const;

private:
	void activateAfterStarting();
	void activateAuthedAccount();
	bool removePasscodeIfEmpty();
	void removeRedundantAccounts();
	void watchSession(not_null<Account*> account);
	void scheduleWriteAccounts();
	void checkForLastProductionConfig(not_null<Main::Account*> account);
	void updateUnreadBadge();
	void scheduleUpdateUnreadBadge();
	void suggestExportIfNeeded();

	const QString _dataName;
	const std::unique_ptr<Storage::Domain> _local;

	std::vector<AccountWithIndex> _accounts;
	rpl::event_stream<> _accountsChanges;
	rpl::variable<Account*> _active = nullptr;
	int _accountToActivate = -1;
	int _lastActiveIndex = -1;
	bool _writeAccountsScheduled = false;

	rpl::event_stream<Session*> _activeSessions;

	rpl::event_stream<> _unreadBadgeChanges;
	int _unreadBadge = 0;
	bool _unreadBadgeMuted = true;
	bool _unreadBadgeUpdateScheduled = false;

	rpl::lifetime _activeLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Main
