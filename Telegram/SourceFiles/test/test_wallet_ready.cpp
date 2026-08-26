/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_wallet_ready.h"

#include "base/flat_map.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "wallet/wallet_session.h"

namespace Test {
namespace {

struct Watch {
	crl::time started = 0;
	crl::time deadline = 0;
	int samples = 0;
	int counts[4] = {};
	WalletRefreshVerdict last;
	QString transitions;
	bool done = false;
};

[[nodiscard]] base::flat_map<QString, Watch> &Watches() {
	static auto result = base::flat_map<QString, Watch>();
	return result;
}

[[nodiscard]] QString Describe(
		const WalletFreshnessReading &reading,
		crl::time window) {
	return u"at=%1 historyAtMs=%2 stateAtMs=%3 historyAgeMs=%4 "
		"stateAgeMs=%5 windowMs=%6"_q
		.arg(qint64(reading.atMs))
		.arg(qint64(reading.historyAtMs))
		.arg(qint64(reading.stateAtMs))
		.arg(qint64(reading.atMs - reading.historyAtMs))
		.arg(qint64(reading.atMs - reading.stateAtMs))
		.arg(qint64(window));
}

void LogSettle(const QString &name, bool settled) {
	LogRaw(u"WALLET_REFRESH_SETTLE: settled=%1 %2"_q
		.arg(settled ? 1 : 0)
		.arg(WalletRefreshSettleDetails(name)));
}

[[nodiscard]] QString PublicReadOut(const Wallet::Session &wallet) {
	auto stateKnown = false;
	auto lifetime = rpl::lifetime();
	wallet.stateKnownValue(
	) | rpl::on_next([&](bool value) {
		stateKnown = value;
	}, lifetime);
	return u"provenEmpty=%1 stateKnown=%2 balanceNano=%3 history=%4 "
		"hasNext=%5 collectibles=%6 sendState=%7"_q
		.arg(wallet.provenEmpty() ? 1 : 0)
		.arg(stateKnown ? 1 : 0)
		.arg(qint64(wallet.balanceNano()))
		.arg(int(wallet.history().size()))
		.arg(wallet.historyHasNext() ? 1 : 0)
		.arg(int(wallet.collectibles().size()))
		.arg(int(wallet.sendState()));
}

} // namespace

QString WalletRefreshStateName(WalletRefreshState state) {
	switch (state) {
	case WalletRefreshState::Unreadable:
		return u"unreadable"_q;
	case WalletRefreshState::Unstamped:
		return u"unstamped"_q;
	case WalletRefreshState::Stale:
		return u"stale"_q;
	case WalletRefreshState::Ready:
		return u"ready"_q;
	}
	return u"missing"_q;
}

WalletRefreshVerdict ReadWalletRefresh(
		const WalletFreshnessReading &reading,
		crl::time window) {
	const auto observation = Describe(reading, window);
	if (reading.atMs <= 0
		|| reading.historyAtMs < 0
		|| reading.stateAtMs < 0
		|| reading.historyAtMs > reading.atMs
		|| reading.stateAtMs > reading.atMs) {
		return { WalletRefreshState::Unreadable, observation };
	} else if (!reading.historyAtMs || !reading.stateAtMs) {
		const auto which = !reading.historyAtMs
			? (!reading.stateAtMs ? u"both"_q : u"history"_q)
			: u"state"_q;
		return {
			WalletRefreshState::Unstamped,
			observation + u" unstamped=%1"_q.arg(which),
		};
	} else if ((reading.atMs - reading.historyAtMs > window)
		|| (reading.atMs - reading.stateAtMs > window)) {
		return { WalletRefreshState::Stale, observation };
	}
	return { WalletRefreshState::Ready, observation };
}

bool WalletRefreshSettled(
		const QString &name,
		Fn<WalletRefreshVerdict()> observe,
		crl::time deadline) {
	auto &watch = Watches()[name];
	if (watch.done) {
		return true;
	}
	const auto now = crl::now();
	if (!watch.started) {
		watch.started = now;
		watch.deadline = deadline;
	}
	const auto first = !watch.samples;
	const auto previous = watch.last.state;
	++watch.samples;
	watch.last = observe();
	++watch.counts[int(watch.last.state)];
	if (first || (watch.last.state != previous)) {
		if (!watch.transitions.isEmpty()) {
			watch.transitions += ',';
		}
		watch.transitions += u"%1@%2"_q
			.arg(WalletRefreshStateName(watch.last.state))
			.arg(qint64(now - watch.started));
	}
	if (watch.last.ready()) {
		watch.done = true;
		LogSettle(name, true);
		Pass(u"wallet refresh settle: %1"_q.arg(name));
		return true;
	} else if (now - watch.started >= watch.deadline) {
		watch.done = true;
		LogSettle(name, false);
		Fail(
			u"wallet refresh settle: %1"_q.arg(name),
			WalletRefreshSettleDetails(name));
		return true;
	}
	return false;
}

QString WalletRefreshSettleDetails(const QString &name) {
	const auto &watches = Watches();
	const auto i = watches.find(name);
	if (i == watches.end()) {
		return u"name=%1 not started"_q.arg(name);
	}
	const auto &watch = i->second;
	const auto tallies = u"unreadable=%1 unstamped=%2 stale=%3 ready=%4"_q
		.arg(watch.counts[int(WalletRefreshState::Unreadable)])
		.arg(watch.counts[int(WalletRefreshState::Unstamped)])
		.arg(watch.counts[int(WalletRefreshState::Stale)])
		.arg(watch.counts[int(WalletRefreshState::Ready)]);
	return u"name=%1 state=%2 observation=%3 samples=%4 %5 elapsedMs=%6 "
		"deadlineMs=%7 transitions=%8"_q
		.arg(name)
		.arg(WalletRefreshStateName(watch.last.state))
		.arg(watch.last.observation.isEmpty()
			? u"<none>"_q
			: watch.last.observation)
		.arg(watch.samples)
		.arg(tallies)
		.arg(qint64(watch.started ? (crl::now() - watch.started) : 0))
		.arg(qint64(watch.deadline))
		.arg(watch.transitions.isEmpty()
			? u"<none>"_q
			: watch.transitions);
}

void AppendWalletRefreshSelfTest(
		not_null<Runner*> runner,
		Fn<Wallet::Session*()> resolve,
		Fn<WalletFreshnessReading()> reading) {
	struct State {
		Wallet::Session *wallet = nullptr;
		bool drained = false;
		crl::time clearedAt = 0;
		crl::time drainedAfterMs = -1;
	};
	const auto state = new State();
	const auto observe = [=] {
		return ReadWalletRefresh(reading());
	};
	const auto readOut = [=] {
		return state->wallet
			? PublicReadOut(*state->wallet)
			: u"wallet=missing"_q;
	};

	runner->add({
		.name = u"wallet readiness self-test: positive"_q,
		.run = [=] {
			state->wallet = resolve();
			Check(
				state->wallet != nullptr,
				u"fixture gate: the wallet session resolved"_q);
			const auto wallet = state->wallet;
			if (!wallet) {
				return;
			}
			// refreshHistory() comes before anything reads _keyState,
			// because its ensureLoaded() is what populates that field:
			// keyState(), startPolling() and debugClearNetworkState()
			// all answer for KeyState::None on a session nothing has
			// touched yet, and debugClearNetworkState() early-returns
			// on exactly that value.
			wallet->refreshHistory();
			const auto key = wallet->keyState();
			Check(
				key != Wallet::KeyState::None,
				u"fixture gate: the test account has a wallet"_q,
				u"keyState=%1"_q.arg(int(key)));
			wallet->startPolling();
		},
		.until = [=] {
			return WalletRefreshSettled(
				u"wallet-ready-self-test-positive"_q,
				observe,
				kWalletSettleTimeout);
		},
		.then = [=] {
			const auto verdict = observe();
			Check(
				verdict.ready(),
				u"an engine refresh reached kReady and stamped both "
				"wallet freshness fields"_q,
				verdict.observation);
			Note(readOut());
		},
		.timeout = kWalletSettleTimeout + kDefaultStageTimeout,
		.timeoutDetails = [] {
			return WalletRefreshSettleDetails(
				u"wallet-ready-self-test-positive"_q);
		},
	});

	runner->add({
		.name = u"wallet readiness self-test: negative"_q,
		.run = [=] {
			const auto wallet = state->wallet;
			if (!wallet) {
				return;
			}
			// Engine::run() always answers on a later main thread turn,
			// so no engine callback can land inside this block and the
			// order below is decided rather than raced. The first
			// refresh puts one in flight and sets _engineRefreshPending;
			// the clear bumps _networkGeneration, zeroes both stamps and
			// clears _historyDone, which is why the observed callback
			// has to be queued after it — a callback queued before is
			// discarded, not drained; the second refreshHistory() then
			// parks that callback on the still-outstanding superseded
			// refresh, because requestEngineRefresh() returns at
			// _engineRefreshPending. The clear also zeroes the polling
			// count, cancels the poll timer and stops the stream, so
			// nothing else can refresh while the state is cleared.
			wallet->refreshHistory();
			wallet->debugClearNetworkState();
			state->clearedAt = crl::now();
			wallet->refreshHistory([=] {
				state->drained = true;
				state->drainedAfterMs = crl::now() - state->clearedAt;
			});
		},
		.until = [=] {
			return state->drained;
		},
		.then = [=] {
			const auto verdict = observe();
			Check(
				verdict.state == WalletRefreshState::Unstamped,
				u"the drained completion stamped neither wallet "
				"freshness field"_q,
				u"drained=1 drainedAfterMs=%1 %2 %3"_q
					.arg(qint64(state->drainedAfterMs))
					.arg(verdict.observation)
					.arg(readOut()));
		},
		.timeout = crl::time(90000),
		.timeoutDetails = [=] {
			return u"drained=%1 drainedAfterMs=%2 %3 %4"_q
				.arg(state->drained ? 1 : 0)
				.arg(qint64(state->drainedAfterMs))
				.arg(observe().observation)
				.arg(readOut());
		},
	});

	runner->add({
		.name = u"wallet readiness self-test: recovery"_q,
		.run = [=] {
			// debugRestoreNetworkState() restores the polling count and
			// calls updatePollingState(), which takes a pollTick()
			// immediately, so the first tick after it is read while both
			// stamps are still zero. That is run 1's reading, reproduced
			// on demand and refused by the same predicate.
			if (const auto wallet = state->wallet) {
				wallet->debugRestoreNetworkState();
			}
		},
		.until = [=] {
			return WalletRefreshSettled(
				u"wallet-ready-self-test-recovery"_q,
				observe,
				kWalletSettleTimeout);
		},
		.then = [=] {
			const auto verdict = observe();
			Check(
				verdict.state == WalletRefreshState::Ready,
				u"the cleared wallet recovered: a later engine refresh "
				"stamped both freshness fields again"_q,
				verdict.observation);
			Note(readOut());
		},
		.timeout = kWalletSettleTimeout + kDefaultStageTimeout,
		.timeoutDetails = [] {
			return WalletRefreshSettleDetails(
				u"wallet-ready-self-test-recovery"_q);
		},
	});

	runner->add({
		.name = u"wallet readiness self-test: refusal text"_q,
		.run = [] {
			const auto zeroed = ReadWalletRefresh({
				.atMs = crl::now(),
				.historyAtMs = 0,
				.stateAtMs = 0,
			});
			Check(
				zeroed.state == WalletRefreshState::Unstamped,
				u"a reading whose stamps were never written is refused "
				"as unstamped, never reported as ready"_q,
				zeroed.observation);
			const auto named = zeroed.observation.contains(u"at="_q)
				&& zeroed.observation.contains(u"historyAgeMs="_q)
				&& zeroed.observation.contains(u"stateAgeMs="_q)
				&& zeroed.observation.contains(u"windowMs="_q);
			Check(
				named,
				u"the refusal text names the last observed values and "
				"the window it judged them against"_q,
				zeroed.observation);
			const auto details = WalletRefreshSettleDetails(
				u"self-test-unstarted"_q);
			const auto explained = details.contains(
				u"self-test-unstarted"_q)
				&& details.contains(u"not started"_q);
			Check(
				explained,
				u"an unstarted settle explains itself by name instead "
				"of reading as a bare not ready"_q,
				details);
		},
	});

	runner->add({
		.name = u"wallet readiness self-test: teardown"_q,
		.run = [=] {
			// This releases the only session-owned state the self-test
			// took. A timed-out stage or the watchdog skips every stage
			// after it, so a run that ends early leaves the polling
			// count raised for the rest of that process.
			if (const auto wallet = state->wallet) {
				wallet->stopPolling();
				Note(u"wallet readiness self-test: pollingRequested=%1"_q
					.arg(wallet->pollingRequested() ? 1 : 0));
			}
		},
	});
}

} // namespace Test

#endif // _DEBUG
