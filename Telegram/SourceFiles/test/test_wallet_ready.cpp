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
	return u"at=%1 stateAtMs=%2 stateAgeMs=%3 windowMs=%4"_q
		.arg(qint64(reading.atMs))
		.arg(qint64(reading.stateAtMs))
		.arg(qint64(reading.atMs - reading.stateAtMs))
		.arg(qint64(window));
}

void LogSettle(const QString &name, bool settled) {
	LogRaw(u"WALLET_REFRESH_SETTLE: settled=%1 %2"_q
		.arg(settled ? 1 : 0)
		.arg(WalletRefreshSettleDetails(name)));
}

// The observation is deliberately not printed here: every caller passes
// it to the neighbouring Check as its |details|, which the harness prints
// on the passing verdict too, so noting it would log the same reading
// twice. Only the label and the state name are carried nowhere else.
void NoteVerdict(
		const QString &label,
		const WalletRefreshVerdict &verdict) {
	Note(u"wallet readiness self-test: %1 state=%2"_q
		.arg(label)
		.arg(WalletRefreshStateName(verdict.state)));
}

[[nodiscard]] QString PublicReadOut(Wallet::Session &wallet) {
	return u"presence=%1 balanceNano=%2 history=%3 hasNext=%4 "
		"collectibles=%5 sendState=%6"_q
		.arg(int(wallet.presence()))
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
		|| reading.stateAtMs < 0
		|| reading.stateAtMs > reading.atMs) {
		return { WalletRefreshState::Unreadable, observation };
	} else if (!reading.stateAtMs) {
		return {
			WalletRefreshState::Unstamped,
			observation + u" unstamped=state"_q,
		};
	} else if (reading.atMs - reading.stateAtMs > window) {
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
			// refreshHistory() is the first touch because its
			// ensureLoaded() is what sends the first wallet.getState:
			// presence(), startPolling() and debugClearNetworkState()
			// all answer for Presence::Unknown on a session nothing has
			// touched yet, and debugClearNetworkState() early-returns
			// on anything but Ready. Nothing here reads the presence
			// either: that request has only just gone out, so the answer
			// deciding it cannot have arrived and a gate on it would
			// refuse a healthy fixture. The settle below is the wait.
			wallet->refreshHistory();
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
			NoteVerdict(u"positive"_q, verdict);
			Check(
				verdict.ready(),
				u"the server's wallet state was applied recently, "
				"stamping the wallet freshness field"_q,
				verdict.observation);
			const auto presence = state->wallet
				? int(state->wallet->presence())
				: -1;
			Check(
				presence == int(Wallet::Presence::Ready),
				u"fixture gate: the test account has a wallet"_q,
				u"presence=%1"_q.arg(presence));
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
			// The clear bumps _networkGeneration, zeroes the stamp and
			// _stateRequestedAt, cancels the outstanding wallet.getState
			// and clears _historyDone — which is why the observed
			// callback has to be queued after it, since a callback
			// queued before is discarded, not drained. It also zeroes
			// the polling count, cancels the poll timer and stops the
			// stream, so nothing can restamp the field while the state
			// is cleared. It deliberately leaves the presence alone, so
			// the refreshHistory() below still runs its body and queues
			// the callback; that queue is now drained by a real
			// wallet.getTransactions answering or failing, so the
			// completion arrives after a network round trip instead of on
			// the spot and the stage timeout is what covers it. That round
			// trip stamps only _historyRefreshedAt, so the field this test
			// reads is still unstamped when the completion fires.
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
			NoteVerdict(u"negative"_q, verdict);
			Check(
				verdict.state == WalletRefreshState::Unstamped,
				u"the drained completion left the wallet freshness "
				"field unstamped"_q,
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
			// immediately, so the first tick after it is read while the
			// stamp is still zero. That is run 1's reading, reproduced
			// on demand and refused by the same predicate. The clear
			// zeroed _stateRequestedAt too, so that tick's refreshState()
			// is not held off by the floor and re-asks at once.
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
			NoteVerdict(u"recovery"_q, verdict);
			Check(
				verdict.state == WalletRefreshState::Ready,
				u"the cleared wallet recovered: a later wallet.getState "
				"stamped the freshness field again"_q,
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
				.stateAtMs = 0,
			});
			NoteVerdict(u"refusal text"_q, zeroed);
			Check(
				zeroed.state == WalletRefreshState::Unstamped,
				u"a reading whose stamps were never written is refused "
				"as unstamped, never reported as ready"_q,
				zeroed.observation);
			const auto named = zeroed.observation.contains(u"at="_q)
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
			const auto atMs = kWalletRefreshWindow * 4;
			const auto staleAtMs = atMs - kWalletRefreshWindow * 2;
			const auto stale = ReadWalletRefresh({
				.atMs = atMs,
				.stateAtMs = staleAtMs,
			});
			NoteVerdict(u"stale"_q, stale);
			Check(
				stale.state == WalletRefreshState::Stale,
				u"a stamp that was written but fell outside the window "
				"is refused as stale, never reported as ready"_q,
				stale.observation);
			const auto edgeAtMs = atMs - kWalletRefreshWindow;
			const auto edge = ReadWalletRefresh({
				.atMs = atMs,
				.stateAtMs = edgeAtMs,
			});
			NoteVerdict(u"window edge"_q, edge);
			Check(
				edge.state == WalletRefreshState::Ready,
				u"a stamp exactly the window old is still ready, so "
				"stale begins past the window and not at it"_q,
				edge.observation);
			const auto unfilled = ReadWalletRefresh(
				WalletFreshnessReading());
			NoteVerdict(u"unfilled"_q, unfilled);
			Check(
				unfilled.state == WalletRefreshState::Unreadable,
				u"a reading the caller never filled in is refused as "
				"unreadable, never treated as fresh"_q,
				unfilled.observation);
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
