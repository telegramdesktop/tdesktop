/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "mtproto/mtproto_response.h"

// A read-before-write transaction over one server fixture an overlay
// mutates and must restore (privacy rules, a username, a global privacy
// setting): a fresh canonical read before any write, no write when it is
// already equal, success only on a fresh complete read equal to the
// requested state, the server's own not-before deadline on a rate refusal,
// one bounded fresh-read retry, finish-owned continuations and a resumable
// ignored obligation record. Examples, the record location and the recovery
// contract: docs/helpers/test_fixture_transaction.md.

namespace Test {

class Runner;

inline constexpr auto kFixtureMaxWait = crl::time(60000);

// The caller's complete canonical state, opaque here and compared only by
// the caller's |equal|. It is persisted in the obligation record, so it
// must not carry credentials: keep user ids, rebuild access hashes inside
// the write function.
using FixtureState = QByteArray;

// The caller's requests. Each call answers |done| or |fail| at most once,
// on the main thread; an answer may come synchronously. A real caller
// sends through api().request(...) with .handleFloodErrors() (or
// .handleAllErrors()): without it MTP::Instance resends a FLOOD_WAIT
// request by itself after the wait, a write without a fresh read. A
// write's |done| is not equality; the transaction always reads again.
struct FixtureTransport {
	Fn<void(
		Fn<void(FixtureState)> done,
		Fn<void(const MTP::Error &)> fail)> read;
	Fn<void(
		const FixtureState &state,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail)> write;
};

// Wall-clock milliseconds since the epoch, so a deadline survives a
// relaunch. Both or neither: empty selects
// QDateTime::currentMSecsSinceEpoch() and the transaction's own
// base::Timer. |schedule| calls |callback| once, at or after |at| by
// |now|; an early call is tolerated, the transaction re-checks the
// deadline and schedules the rest without sending anything.
struct FixtureClock {
	Fn<crl::time()> now;
	Fn<void(crl::time at, Fn<void()> callback)> schedule;
};

// |label|: public, [A-Za-z0-9_-]+, names the record file and every row.
// |target|: the caller's explicit target identity (account and fixture
// key); stored in the record, compared on load, never logged. The
// transaction never picks an account: the caller's transport and
// |authorize| decide which one is written.
// |authorize|: required, read before every request; a non-empty answer
// refuses that request and is logged, so it names a reason, never a
// secret.
// |equal|: the complete caller-defined equality, including meaningful
// absence and order.
// |recordDir|: empty selects the live portable copy's
// test_fixture_obligations/ folder; only the self-test overrides it.
// |maxWait|: the longest server deadline the transaction waits for in
// process; a later one leaves the operation unresolved with the deadline
// recorded.
struct FixtureTransactionArgs {
	QString label;
	QString target;
	Fn<QString()> authorize;
	FixtureTransport transport;
	Fn<bool(const FixtureState &, const FixtureState &)> equal;
	FixtureClock clock;
	QString recordDir;
	crl::time maxWait = kFixtureMaxWait;
};

enum class FixtureOperation {
	None,
	Transition,
	Restore,
};

enum class FixtureOutcome {
	None,
	Equal,
	Written,
	Unresolved,
	Refused,
};

// The one reading. Filled at construction from the loaded record
// (operation None; outcome Refused with the reason when the arguments or
// the record refuse; |pending| and |notBefore| as recorded), then by each
// operation: |reads|, |writes| and |waits| count that operation's
// requests and deadline waits, |pending| and |notBefore| are the record
// as it stands (|notBefore| 0 when none was ever recorded).
//
// Outcomes and reasons:
// - Equal: a fresh read taken before any further write equalled the
//   target (a write refused earlier in the operation still counts in
//   |writes|).
// - Written: a write was sent and a fresh read after it equalled the
//   target.
// - Unresolved: verify-unequal (the read after a write differs),
//   rate-retry-exhausted (a second rate refusal in one operation),
//   deadline-beyond-bound (the deadline is more than |maxWait| away),
//   record-not-persisted (a deadline or a cleared obligation could not be
//   written; the record keeps its last written state), released
//   (release() during the operation), or the server error type of any
//   other failure, with no wait. The obligation stays as recorded.
// - Refused, without sending the request it stopped at: invalid-label,
//   no-authorization, incomplete-arguments, malformed-record,
//   foreign-record (the record names another target), released,
//   pending-restoration (a transition while a record loaded from an
//   earlier invocation is still pending), no-obligation (a restore with no
//   original this instance may write), record-not-persisted (the
//   obligation could not be written, so the write was not sent), and
//   "unauthorized: <authorize text>" (|authorize| refused that request;
//   no obligation is recorded for a write that was not sent).
struct FixtureResult {
	FixtureOperation operation = FixtureOperation::None;
	FixtureOutcome outcome = FixtureOutcome::None;
	QString reason;
	crl::time notBefore = 0;
	int reads = 0;
	int writes = 0;
	int waits = 0;
	bool pending = false;
};

// One fixture, one record: <recordDir>/<label>.json, holding the schema,
// the label, the target, the pending flag, the complete original (once
// captured) and the not-before deadline. It is written only before each
// write (pending with the original), on each rate refusal (the deadline)
// and when a restore's fresh read equals the original (pending cleared).
// A no-op transition writes no file. A malformed record, or one bound to
// another target, refuses every operation and is never overwritten. A
// resolved record keeps its original, but authorizes no restore by a later
// instance.
//
// Main thread only. One operation at a time (busy() is false between
// them); its result is final once busy() turns false. Every transport and
// scheduler continuation is guarded on this object and does nothing after
// release() or destruction.
class FixtureTransaction final : public base::has_weak_ptr {
public:
	explicit FixtureTransaction(FixtureTransactionArgs &&args);

	// Moves the fixture to |requested|. Reads first; an equal read is
	// Equal with no write and no record change. Otherwise, unless this
	// instance already owns a pending obligation, the read becomes the
	// original, the record is written pending with it, then the write is
	// sent and a fresh read decides. Further transitions of the instance
	// that owns the obligation keep the first original. A record left
	// pending by an earlier invocation refuses (pending-restoration):
	// restore first.
	void transition(FixtureState requested);

	// Puts the original back: the pending record loaded at construction,
	// or the original this instance captured. Reads first; an equal read
	// is Equal with no write, clearing a pending record. Otherwise the
	// write and a fresh read as in transition(); an equal verify clears the
	// pending record. Repeating it after success is a fresh-read no-op.
	void restore();

	// Once; stops this instance for good: cancels the timer, makes every
	// pending continuation a no-op and turns a running operation into
	// Unresolved "released". Never writes the record, so a pending
	// obligation stays for a later launch. Writes the release row when it
	// stopped an operation or an obligation is pending.
	void release();

	[[nodiscard]] bool busy() const;
	[[nodiscard]] const FixtureResult &result() const;

private:
	struct Record {
		FixtureState original;
		crl::time notBefore = 0;
		bool pending = false;
		bool originalKnown = false;
	};
	enum class RecordLoad {
		Absent,
		Valid,
		Malformed,
		Foreign,
	};

	[[nodiscard]] static RecordLoad LoadRecord(
		const QString &path,
		const QString &label,
		const QString &target,
		Record &record);
	[[nodiscard]] static bool SaveRecord(
		const QString &path,
		const QString &label,
		const QString &target,
		const Record &record);

	[[nodiscard]] crl::time now() const;
	[[nodiscard]] bool persist(const Record &record) const;
	[[nodiscard]] bool authorized();
	void begin(FixtureOperation operation, FixtureState target);
	void proceed();
	void wait(crl::time at);
	void resume();
	void sendRead();
	void sendWrite();
	void onRead(FixtureState current);
	void onWritten();
	void onError(const MTP::Error &error);
	void finishRestored(FixtureOutcome outcome);
	void finish(FixtureOutcome outcome, const QString &reason = QString());
	void conclude(FixtureOutcome outcome, const QString &reason);

	FixtureTransactionArgs _args;
	QString _path;
	QString _refusal;
	Record _record;
	FixtureState _target;
	FixtureResult _result;
	base::Timer _timer;
	bool _verifying = false;
	bool _ownsObligation = false;
	bool _originalUsable = false;
	bool _busy = false;
	bool _released = false;

};

// The entry point overlays use: constructs the transaction and registers a
// Runner::onFinish release holding it weakly, so every finish path (normal
// completion, skip-to-end, stage timeout, watchdog) stops its continuations
// and leaves the record as it stands. Never null.
//
// Rows (test log only; never the state, the original, the target or the
// record path; deadlines as milliseconds from now):
//   NOTE: FIXTURE_TRANSACTION: label=<l> op=<none|transition|restore>
//     outcome=<none|equal|written|unresolved|refused> reason=[<r>]
//     reads=<n> writes=<n> waits=<n> pending=<0|1>
//     notBeforeInMs=<ms|none>
//   - at construction when refused or a pending record was loaded, and
//     once per finished operation;
//   NOTE: FIXTURE_TRANSACTION: label=<l> op=<op> rate=[<error type>]
//     notBeforeInMs=<ms>
//   - on each rate refusal, after the deadline is recorded;
//   NOTE: FIXTURE_TRANSACTION: label=<l> released busy=<0|1>
//     pending=<0|1> notBeforeInMs=<ms|none>
//   - from release(), as described there.
[[nodiscard]] std::shared_ptr<FixtureTransaction> OpenFixtureTransaction(
	not_null<Runner*> runner,
	FixtureTransactionArgs args);

// The transaction measuring itself through OpenFixtureTransaction with a
// fake local transport and, for all but one leg, a fake clock: zero-write
// equality, one necessary write with a fresh verify read, the record
// written before the write, not-before scheduling (early firing included),
// the default wall-clock timer, an equal read at the deadline, bounded
// refusal, release, a destroyed owner, resumption from the record file by
// a later instance, exact and unequal originals, authorization and target
// refusals, the finish release and log secrecy. Records go under
// EvidenceDir()/fixture-transaction-self-test/, never the live copy's
// folder. Needs no session, network, account or fixture secret; every row
// is expected to PASS.
void AppendFixtureTransactionSelfTest(not_null<Runner*> runner);

} // namespace Test
