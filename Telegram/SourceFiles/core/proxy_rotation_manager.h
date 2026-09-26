/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/proxy_check.h"

#include <rpl/lifetime.h>
#include <vector>

namespace Main {
class Account;
} // namespace Main

namespace Core {

class ProxyRotationManager final {
public:
	ProxyRotationManager();

	void settingsChanged();
	void handleConnectionStateChanged(
		not_null<Main::Account*> account,
		int32 state);

private:
	struct Entry {
		MTP::ProxyData proxy;
		MTP::ProxyCheckConnection v4;
		MTP::ProxyCheckConnection v6;
		bool checking = false;
		crl::time startedAt = 0;
		crl::time availableAt = 0;
	};

	[[nodiscard]] bool shouldObserve() const;
	[[nodiscard]] std::vector<not_null<Main::Account*>> productionAccounts() const;
	[[nodiscard]] not_null<Main::Account*> accountForChecks() const;
	[[nodiscard]] Entry *find(const MTP::ProxyData &proxy);
	[[nodiscard]] Entry &ensure(const MTP::ProxyData &proxy);

	void reevaluate();
	void startChecking();
	void stopChecking();
	void pruneRemovedEntries();
	void updateProbeOrder();
	void continueChecking(crl::time delay);
	void runChecks();
	void pruneExpiredChecks();
	void startNextCheck();
	void switchTimerDone();
	void clearPendingChecks();
	void checkDone(
		const MTP::ProxyData &proxy,
		not_null<MTP::details::AbstractConnection*> raw,
		int ping);
	void checkFailed(
		const MTP::ProxyData &proxy,
		not_null<MTP::details::AbstractConnection*> raw);
	[[nodiscard]] bool switchToAvailable();
	[[nodiscard]] bool shouldSwitchToAvailable() const;

	base::Timer _checkTimer;
	base::Timer _switchTimer;
	std::vector<Entry> _entries;
	std::vector<int> _probeOrder;
	int _nextCheckIndex = 0;
	bool _checking = false;
	bool _waitingToSwitch = false;
	crl::time _switchStartedAt = 0;
	rpl::lifetime _lifetime;

};

} // namespace Core
