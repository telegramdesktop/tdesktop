/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

namespace Wallet {
class Session;
} // namespace Wallet

namespace Test {

class Runner;

// Wallet::Session::_stateRefreshedAt is the one field that says the
// server's wallet state reached this client, and it is what this predicate
// measures. applyState() stamps it, and applyState() is the single apply
// path for both the wallet.getState answer and a pushed updateWalletState.
//
// Wallet::Session::refreshHistory(done) cannot stand in for it. Its
// ensureLoaded() is what sends the very first wallet.getState, but the
// callback says nothing about that answer: refreshHistory() runs |done| at
// once when the presence is not Ready or the lane is paged, and otherwise
// queues it on _historyDone, which finishHistoryWaiters() drains when the
// wallet.getTransactions it issued answers or fails. So on a Ready presence
// the completion does wait for a real round trip — of the transaction
// history lane, which stamps _historyRefreshedAt. Only applyState() writes
// _stateRefreshedAt; engine refreshes stamp _engineRefreshedAt instead.
// A history completion therefore still carries no wallet-state freshness
// information whatsoever.
//
// Nothing outside Wallet::Session can read the stamp: it is private and has
// no accessor. pollTick()'s stale() cannot stand in for it either, because
// stale(0) is permanently true, so an unstamped run is byte-indistinguishable
// from a genuinely stale one.
//
// Run 1 of 2026/08/26/repair-and-retune-wallet-history-debug-scenarios lost
// a full normal campaign run to exactly that. Its fixture gate waited on the
// refreshHistory(done) completion, which arrived with the stamp still zero,
// and the row behind it read "atMs=9957 stateAgeMs=9957" — the age equal to
// the row's own timestamp, because an unwritten stamp reads crl::now() - 0.
//
// WalletFreshnessReading carries the raw stamp rather than an age, so "never
// written" is the exact test !stateAtMs instead of a margin heuristic that
// refuses a genuine early stamp. A caller holding only an age converts
// losslessly by passing atMs - age: an unwritten stamp's age equals atMs,
// which maps back to exactly 0.
//
// A wallet readiness wait is a FIXTURE GATE for a scenario whose subject
// is not the wallet state lane itself: reverting such a diff cannot
// change the reading, so a refusal makes the run a harness or environment
// failure (TEST_FLAW) and the acceptance criteria N/A, never a product
// FAIL. When the diff under test touches refreshState(), requestState(),
// applyState(), applyUpdate() or clearNetworkState(), the readiness
// condition itself is the behavior under test and a refusal is a product
// FAIL — README.md's stage contract states the same exception.

// The 30-second freshness window matches kStreamResyncInterval. The
// predicate measures server state alone; engine polling accepts freshness
// from either the server state or the engine. refreshState() floors its
// requests at twice this window, so a server stamp between one and two
// windows old reads Stale until a later state request or push replaces it.
// The settle deadline below has to outlast that request floor.
inline constexpr auto kWalletRefreshWindow = 30 * crl::time(1000);

// Fifteen 5s poll ticks — past refreshState()'s own request floor, so a
// settle that has to wait a whole floor out still resolves inside it, and an
// unreachable server ends the wait instead of hanging it.
inline constexpr auto kWalletSettleTimeout = crl::time(75000);

enum class WalletRefreshState {
	Unreadable,
	Unstamped,
	Stale,
	Ready,
};

[[nodiscard]] QString WalletRefreshStateName(WalletRefreshState state);

// One observation of the stamp taken at one instant: |atMs| is crl::now()
// at the reading, and |stateAtMs| is that field's raw value. A stamp of 0
// means the field was never written; -1 means the caller supplied nothing.
// Both are refused, never treated as fresh.
struct WalletFreshnessReading {
	crl::time atMs = 0;
	crl::time stateAtMs = -1;
};

// |observation| is never empty: it carries the values the verdict was read
// from, so a caller printing the verdict prints the evidence it judged.
struct WalletRefreshVerdict {
	WalletRefreshState state = WalletRefreshState::Unreadable;
	QString observation;

	[[nodiscard]] bool ready() const {
		return state == WalletRefreshState::Ready;
	}
};

// The single place the refusal lives, and the one a caller cannot opt out
// of: a stamp still at 0 is Unstamped — never Stale, never Ready. That is
// the exact reading which cost the lost run named above, refused here
// instead of in every scenario that has to remember to. Pure: it reads no
// clock of its own and takes every value from |reading|.
[[nodiscard]] WalletRefreshVerdict ReadWalletRefresh(
	const WalletFreshnessReading &reading,
	crl::time window = kWalletRefreshWindow);

// Call from a stage's |until| with a stable |name|. |name| is single-use
// per process: the watch registry is a process-global map that is never
// cleared and a settled or refused watch latches done, so a second call
// with the same name returns true on its first tick without observing and
// without logging a line. Namespace |name| to the scenario that owns it.
// Returns false while it should keep polling and true exactly once when it
// is done: Pass on the first Ready verdict, Fail at |deadline| with
// WalletRefreshSettleDetails(). |observe| must be pure — it must not issue
// a refresh, age a stamp or clear state; a scenario that wants a refresh
// issues it in the stage's |run|.
//
// The one-shot emission at the deadline is the deliberate, precedented
// exception to "|until| must not emit a PASS/FAIL" (PanelShowSettled does
// the same for the same reason): it happens at most once per named watch and
// converts an opaque stage timeout into a named fixture-gate refusal.
[[nodiscard]] bool WalletRefreshSettled(
	const QString &name,
	Fn<WalletRefreshVerdict()> observe,
	crl::time deadline = kWalletSettleTimeout);

// The single formatter of a watch's fields: the WALLET_REFRESH_SETTLE line
// and the deadline refusal print the same list through this, so a passing
// run shows exactly what a refusal would have shown. A watch that has never
// been polled returns a line naming it and "not started", never a bare
// "not ready".
[[nodiscard]] QString WalletRefreshSettleDetails(const QString &name);

// Appends the instrument's own positive / negative / recovery / refusal-text
// self-test, plus its teardown, as five stages on |runner|. |resolve| hands
// back the session and |reading| takes one observation of its stamp.
//
// The negative case is the point: debugClearNetworkState() zeroes the stamp,
// cancels the outstanding wallet.getState and stops the poll, so nothing can
// restamp the field; it leaves the presence alone, so the refreshHistory()
// after it still runs its body and queues the observed callback. That
// callback is drained when the wallet.getTransactions it issued answers or
// fails, so the completion arrives after a network round trip that the stage
// timeout has to cover, and that round trip stamps _historyRefreshedAt,
// never the _stateRefreshedAt this self-test reads.
//
// This routine does mutate the session: it starts polling, issues refreshes
// and clears and restores network state. That is why it is a named
// self-test and not part of the predicate, which never mutates anything. It
// appends its own teardown stage releasing the polling count it took, and it
// emits no deliberate failure — every stage it appends is expected to PASS
// on a healthy fixture.
void AppendWalletRefreshSelfTest(
	not_null<Runner*> runner,
	Fn<Wallet::Session*()> resolve,
	Fn<WalletFreshnessReading()> reading);

} // namespace Test
