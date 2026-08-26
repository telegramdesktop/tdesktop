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

// Wallet::Session::refreshHistory(done) queues |done| on _historyDone, and
// requestEngineRefresh()'s local finishHistoryWaiters() drains that queue on
// every outcome: immediately when the engine is stopping or has no client,
// on the engine error path, in both engine callbacks when the refresh was
// superseded by a network generation bump, and after applyEngineUpdate() —
// which itself returns at its outcome gate for anything but kCompleted, and
// whose applyEngineActivity() leg writes nothing unless the activity
// resource phase is kReady. A completed refreshHistory(done) callback
// therefore carries no information about whether _historyRefreshedAt and
// _stateRefreshedAt were written.
//
// Nothing outside Wallet::Session can read those two stamps: both are
// private and neither has an accessor. debugSetRefreshAges() cannot stand in
// for one, because its `if (field)` guard silently no-ops on a stamp still
// at zero; pollTick()'s stale() cannot either, because stale(0) is
// permanently true, so an unstamped run is byte-indistinguishable from a
// genuinely stale one.
//
// Run 1 of 2026/08/26/repair-and-retune-wallet-history-debug-scenarios lost
// a full normal campaign run to exactly that. Its fixture gate waited on the
// refreshHistory(done) completion, which arrived with both stamps still
// zero, and the row behind it read "atMs=9957 historyAgeMs=9957
// stateAgeMs=9957" — every age equal to the row's own timestamp, because an
// unwritten stamp reads crl::now() - 0.
//
// WalletFreshnessReading carries the raw stamps rather than ages, so "never
// written" is the exact test !historyAtMs || !stateAtMs instead of a margin
// heuristic that refuses a genuine early stamp. A caller holding only ages
// converts losslessly by passing atMs - age: an unwritten stamp's age equals
// atMs, which maps back to exactly 0.
//
// provenEmpty() is the only public read-out whose truth implies both stamps
// were written, because its fresh() lambda short-circuits on `completed &&`
// and fresh(0) is false. It certifies more than this predicate asks and lags
// it: it additionally demands an empty, zero-balance, non-paging,
// non-sending wallet and fresh(_collectiblesCompletedAt), which a separate
// engine round trip writes. A false answer attributes to no particular term,
// and _sendUnresolved is the one term of that conjunction with no public
// read-out at all.
//
// A wallet readiness wait is a FIXTURE GATE for a scenario whose subject
// is not the wallet refresh path itself: reverting such a diff cannot
// change the reading, so a refusal makes the run a harness or environment
// failure (TEST_FLAW) and the acceptance criteria N/A, never a product
// FAIL. When the diff under test touches requestEngineRefresh(),
// applyEngineUpdate(), applyEngineActivity() or clearNetworkState(), the
// readiness condition itself is the behavior under test and a refusal is a
// product FAIL — README.md's stage contract states the same exception.

// The window pollTick()'s stale() uses (kStreamResyncInterval), so "ready"
// means the poll itself would not call this reading stale.
inline constexpr auto kWalletRefreshWindow = 30 * crl::time(1000);

// Twelve 5s poll ticks plus margin — well past the one or two engine round
// trips a settle needs, so an unreachable proxy ends the wait, not hangs it.
inline constexpr auto kWalletSettleTimeout = crl::time(75000);

enum class WalletRefreshState {
	Unreadable,
	Unstamped,
	Stale,
	Ready,
};

[[nodiscard]] QString WalletRefreshStateName(WalletRefreshState state);

// One observation of both stamps taken at one instant: |atMs| is crl::now()
// at the reading, and each stamp is that field's raw value. A stamp of 0
// means the field was never written; -1 means the caller supplied nothing.
// Both are refused, never treated as fresh.
struct WalletFreshnessReading {
	crl::time atMs = 0;
	crl::time historyAtMs = -1;
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
// back the session and |reading| takes one observation of its two stamps.
//
// The negative case is the point: refreshHistory() goes out first, so a
// refresh is in flight and _engineRefreshPending is set; then
// debugClearNetworkState() bumps _networkGeneration, zeroes both stamps and
// clears _historyDone — which is why the observed callback must be queued
// after it, since a callback queued before is discarded, not drained; then
// refreshHistory(done) parks that callback on the still-outstanding
// superseded refresh, because requestEngineRefresh() returns at
// _engineRefreshPending. The completion then fires with nothing stamped.
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
