/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"

namespace Storage {
class Domain;
class PasscodeDerivation;
enum class StartResult : uchar;
} // namespace Storage

namespace MTP {
enum class Environment : uchar;
} // namespace MTP

namespace Wallet {
class VaultRuntime;
} // namespace Wallet

namespace Main {

class Account;
class Session;

class Domain final : public base::has_weak_ptr {
public:
	struct AccountWithIndex {
		int index = 0;
		std::unique_ptr<Account> account;
	};

	static constexpr auto kMaxAccounts = 3;
	static constexpr auto kPremiumMaxAccounts = 6;

	explicit Domain(const QString &dataName);
	~Domain();

	[[nodiscard]] bool started() const;
	[[nodiscard]] Storage::StartResult start(const QByteArray &passcode);

	// One passcode attempt at a time for the whole application: while the
	// derivation runs on a worker this returns true, and every further call
	// answers false without starting anything or ever invoking its done.
	// The verdict is applied here on the main thread - a cold attempt starts
	// the domain, a warm one only checks the passcode - and done(correct)
	// runs last. An attempt whose started-ness changed underneath it (the
	// lock screen's Log out started the domain from scratch, or finish() ran
	// during shutdown) applies nothing and reports the passcode incorrect.
	// An attempt answered while the application is quitting applies nothing
	// and drops its done as well: finish() leaves a cold domain as unstarted
	// as it found it, so started-ness alone cannot tell a quitting cold
	// attempt from one that should start every account. An attempt that
	// outlives the domain is dropped together with its done.
	[[nodiscard]] bool tryPasscode(
		const QByteArray &passcode,
		Fn<void(bool correct)> done);

	void resetWithForgottenPasscode();
	void finish();

	[[nodiscard]] int maxAccounts() const;
	[[nodiscard]] rpl::producer<int> maxAccountsChanges() const;

	[[nodiscard]] Storage::Domain &local() const {
		return *_local;
	}

	[[nodiscard]] Wallet::VaultRuntime &walletKeyring();

	[[nodiscard]] auto accounts() const
		-> const std::vector<AccountWithIndex> &;
	[[nodiscard]] std::vector<not_null<Account*>> orderedAccounts() const;
	[[nodiscard]] rpl::producer<Account*> activeValue() const;
	[[nodiscard]] rpl::producer<> accountsChanges() const;
	[[nodiscard]] Account *maybeLastOrSomeAuthedAccount();
	[[nodiscard]] int accountsAuthedCount() const;

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
	void addActivated(MTP::Environment environment, bool newWindow = false);

	// Drops session-less accounts that have no window open for them.
	void removeRedundantAccounts();

	// Interface for Storage::Domain.
	void accountAddedInStorage(AccountWithIndex accountWithIndex);
	void activateFromStorage(int index);
	[[nodiscard]] int activeForStorage() const;

private:
	[[nodiscard]] Storage::StartResult startWith(
		Storage::PasscodeDerivation derived);
	void activateAfterStarting();
	void closeAccountWindows(not_null<Main::Account*> account);

	// Answers whether a removal ran whose checked write already persisted
	// the current accounts info - not whether a passcode is gone, which
	// would make the no-passcode case skip the caller's accounts write.
	bool removePasscodeIfEmpty();

	// True when a completed last logout has left the passcode guarding
	// nothing that can still be asked for: one account, its session gone,
	// and a passcode still installed. The one definition both the logout
	// call site and the retry read.
	[[nodiscard]] bool passcodeRemovalAuthorized() const;
	bool clearPasscodeAfterLastLogout();
	void reportFailedPasscodeClear();

	// Retries the removal a completed last logout authorized, after its
	// checked write did not reach the disk. The authorization is re-read
	// here and never carried over from the failed attempt, so a passcode
	// already gone, an account signed in again or a second account all
	// make this do nothing at all. Answers whether this call removed the
	// passcode, so the caller can say so.
	[[nodiscard]] bool finishPasscodeClearAfterReset();

	void watchSession(not_null<Account*> account);
	void scheduleWriteAccounts();
	void checkForLastProductionConfig(not_null<Main::Account*> account);
	void updateUnreadBadge();
	void scheduleUpdateUnreadBadge();
	void suggestExportIfNeeded();

	const QString _dataName;
	const std::unique_ptr<Storage::Domain> _local;

	std::shared_ptr<Wallet::VaultRuntime> _walletKeyring;
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
	bool _passcodeDeriving = false;

	rpl::variable<int> _lastMaxAccounts;

	rpl::lifetime _activeLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Main
