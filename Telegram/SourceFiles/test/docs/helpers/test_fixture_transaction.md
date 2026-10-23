# test_fixture_transaction

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (with its own self-test).
Source contract: [test_fixture_transaction.h](../../test_fixture_transaction.h).

## Use when

An overlay mutates a server fixture that it must restore - privacy rules
(`account.getPrivacy` / `account.setPrivacy`), a username, a global privacy
setting - in one or several legs of a campaign. `Test::OpenFixtureTransaction`
reads canonical state before any write, sends no write when the state already
equals the requested one, accepts a write only when a fresh complete read
equals it, waits the server's own not-before deadline on a rate refusal before
one fresh-read retry, and keeps the complete original in an ignored record a
later launch resumes. It is one fixture's read/compare/write obligation, not
account automation: the caller owns the state, its equality, the
authorization and the target.

For a local in-memory setting use [test_notify_override](test_notify_override.md);
for controlled replies into a real session, [test_rpc_fixture](test_rpc_fixture.md);
wallet custody is [test_gram_reconcile](test_gram_reconcile.md).

## Preconditions and limits

- The caller defines the fixture. `FixtureState` is an opaque `QByteArray`
  holding the complete canonical state, and `equal` is its complete
  equality, including what absence and order mean: whether people order
  matters, whether a rule that lists nobody equals an absent rule. The
  transaction compares nothing itself.
- No credentials in the state: it is persisted in the record. Keep user ids
  and rebuild input users (access hashes) from the live session inside
  `write`.
- `transport.read` and `transport.write` answer `done` or
  `fail(const MTP::Error &)` at most once per call, on the main thread,
  possibly synchronously. A write's `done` is not equality: the transaction
  always reads again.
- Real requests go through `api().request(...)` with `.handleFloodErrors()`
  (or `.handleAllErrors()`). With the default handling a `FLOOD_WAIT_<n>`
  never reaches `fail`: `MTP::Instance` resends the same request by itself
  after the wait, a write with no fresh read and no recorded deadline. The
  fake-transport self-test does not prove a caller's routing.
- `authorize` is required and read before every request; a non-empty answer
  refuses that request. It is the caller's account authorization (for
  example: the exact Test session the scenario runs on still exists), and its
  text is logged, so it names a reason, never a secret. The transaction never
  picks an account: the transport and `authorize` decide which one is
  written.
- `target` names the account and the fixture key, for example the Test
  user's id and the privacy key. It is stored in the record, compared on load
  and never logged.
- `label` is public, `[A-Za-z0-9_-]+`, one per fixture: it names the record
  file and every row.
- Deadlines are wall-clock milliseconds since the epoch
  (`QDateTime::currentMSecsSinceEpoch()`; only the self-test passes a
  `FixtureClock`), so a recorded deadline survives a relaunch, which
  `crl::now()` would not. An early timer is re-checked and sends nothing.
- `maxWait` (default `Test::kFixtureMaxWait`, 60 s) bounds an in-process
  wait; an operation waits for a deadline at most once.
- A stage awaiting an operation (`until` `!busy()`) needs a timeout above
  `maxWait` plus the requests' own time, for example
  `Test::kFixtureMaxWait + crl::time(30000)`, and `TDESKTOP_TEST_WATCHDOG`
  (120 s by default) must cover every such wait of the run. A stage timeout
  or the watchdog releases the transaction and leaves the obligation for a
  later launch.
- Main thread only, one operation at a time: `transition()` or `restore()`
  while `busy()` is a contract violation.

## Minimal use

An overlay sketch against the real API; no Telegram translation unit
compiles it (the self-test compiles the API with a fake transport).
`PhonePrivacyState`, `PhonePrivacyEqual`, `PhonePrivacyInput`, `NobodyState`,
`ScenarioSession` and `AppendPhoneHiddenChecks` stand for the overlay's own
code: the canonical state of `MTPaccount_PrivacyRules` as rule kinds and user
ids, its complete equality, the input rules rebuilt from the live session,
the requested state, the exact Test session the scenario runs on, and the
dependent checks.

```cpp
constexpr auto kFixtureStageTimeout = Test::kFixtureMaxWait
	+ crl::time(30000);

[[nodiscard]] Test::FixtureTransactionArgs PhonePrivacyArgs(
		not_null<Main::Session*> session) {
	const auto weak = base::make_weak(session);
	const auto key = MTP_inputPrivacyKeyPhoneNumber();
	return {
		.label = u"phone-number-privacy"_q,
		.target = u"user-%1:phone-number-privacy"_q.arg(
			session->userId().bare),
		.authorize = [=] {
			const auto strong = weak.get();
			return (strong && strong->isTestMode())
				? QString()
				: u"the scenario's Test session is gone"_q;
		},
		.transport = {
			// authorize() was read just before each call.
			.read = [=](
					Fn<void(Test::FixtureState)> done,
					Fn<void(const MTP::Error &)> fail) {
				weak.get()->api().request(MTPaccount_GetPrivacy(
					key
				)).done([=](const MTPaccount_PrivacyRules &result) {
					done(PhonePrivacyState(result));
				}).fail([=](const MTP::Error &error) {
					fail(error);
				}).handleFloodErrors().send();
			},
			.write = [=](
					const Test::FixtureState &state,
					Fn<void()> done,
					Fn<void(const MTP::Error &)> fail) {
				const auto strong = weak.get();
				strong->api().request(MTPaccount_SetPrivacy(
					key,
					PhonePrivacyInput(strong, state)
				)).done([=] {
					done();
				}).fail([=](const MTP::Error &error) {
					fail(error);
				}).handleFloodErrors().send();
			},
		},
		.equal = PhonePrivacyEqual,
	};
}

void SetupScenario(not_null<Runner*> runner) {
	struct State {
		std::shared_ptr<Test::FixtureTransaction> fixture;
		QString gate;
	};
	const auto state = std::make_shared<State>();
	const auto session = Test::UseTestSession(runner);
	runner->waitEvent(u"launch_finished"_q);
	runner->waitForSessionReady();
	runner->add({
		.name = u"phone privacy fixture: open"_q,
		.run = [=] {
			state->fixture = Test::OpenFixtureTransaction(
				runner,
				PhonePrivacyArgs(ScenarioSession()));
			if (state->fixture->result().pending) {
				// An earlier launch left it mutated: restore first.
				state->fixture->restore();
			}
		},
		.until = [=] { return !state->fixture->busy(); },
		.then = [=] {
			const auto &result = state->fixture->result();
			const auto ready = !result.pending
				&& (result.outcome != Test::FixtureOutcome::Refused);
			Test::Check(
				ready,
				u"fixture gate: phone privacy fixture ready"_q,
				u"reason=[%1] writes=%2"_q
					.arg(result.reason)
					.arg(result.writes));
			if (!ready) {
				state->gate = u"fixture gate: phone privacy not ready"_q;
			}
		},
		.timeout = kFixtureStageTimeout,
	});
	runner->addGated([=] { return state->gate; }, [&] {
		runner->add({
			.name = u"phone privacy fixture: nobody"_q,
			.run = [=] { state->fixture->transition(NobodyState()); },
			.until = [=] { return !state->fixture->busy(); },
			.then = [=] {
				const auto &result = state->fixture->result();
				const auto applied
					= (result.outcome == Test::FixtureOutcome::Equal)
					|| (result.outcome == Test::FixtureOutcome::Written);
				Test::Check(
					applied,
					u"fixture gate: phone privacy is nobody"_q,
					u"reason=[%1] writes=%2 waits=%3"_q
						.arg(result.reason)
						.arg(result.writes)
						.arg(result.waits));
				if (!applied) {
					state->gate = u"fixture gate: phone privacy not "
						"applied"_q;
				}
			},
			.timeout = kFixtureStageTimeout,
		});
		AppendPhoneHiddenChecks(runner); // Dependent: inherits the gate.
		runner->addRecovery({
			.name = u"phone privacy fixture: restore"_q,
			.skipReason = [=] {
				return state->fixture->result().pending
					? QString()
					: u"applies=0: no phone privacy obligation"_q;
			},
			.run = [=] { state->fixture->restore(); },
			.until = [=] { return !state->fixture->busy(); },
			.then = [=] {
				const auto &result = state->fixture->result();
				Test::Check(
					!result.pending,
					u"phone privacy fixture: the original restored"_q,
					u"reason=[%1] writes=%2"_q
						.arg(result.reason)
						.arg(result.writes));
			},
			.timeout = kFixtureStageTimeout,
		});
	});
	Test::AppendTestSessionRestore(runner, session);
}
```

The restoration is declared with `addRecovery`, so a refused transition never
skips it ([gated groups](test_runner.md#gated-groups-and-recovery-stages));
its own `skipReason` is the policy: restore while the record is pending.
A campaign with several legs keeps one transaction per fixture: each further
`transition()` of the instance that owns the obligation keeps the first
original, and one restoration at the end, or between legs, puts it back.

## Readings and refusals

`result()` is the one reading: `operation`, `outcome`, `reason`, this
operation's `reads`, `writes` and `waits`, and the record as it stands,
`pending` and `notBefore` (0 when no deadline was ever recorded). It is filled
at construction from the loaded record (operation `None`) and final once
`busy()` turns false. `Equal` and `Written` both mean a fresh read equalled
the target; every other outcome decides nothing about the fixture, and a
refusal is never a PASS.

| Outcome | Meaning and reasons |
| --- | --- |
| `Equal` | A fresh read taken before any further write equalled the target: no write was needed (a write refused earlier in the same operation still counts in `writes`). |
| `Written` | A write was sent and a fresh complete read after it equalled the target. |
| `Unresolved` | `verify-unequal` (the read after a write differs), `rate-retry-exhausted` (a second rate refusal in one operation), `deadline-beyond-bound` (the deadline is more than `maxWait` away), `record-not-persisted` (a deadline or a cleared obligation could not be written), `released` (`release()` during the operation), or the server's error type for any other failure, with no wait. The obligation stays as recorded. |
| `Refused` | No request was sent where it stopped: `invalid-label`, `no-authorization`, `incomplete-arguments`, `malformed-record`, `foreign-record` (the record names another target), `released`, `pending-restoration` (a transition while a record loaded from an earlier invocation is pending), `no-obligation` (a restore with no original this instance may write), `record-not-persisted` (the obligation could not be written, so the write was not sent), `unauthorized: <authorize text>`. |

A rate refusal is `FLOOD_WAIT_<n>` or `FLOOD_PREMIUM_WAIT_<n>` in
`error.type()`: the deadline becomes the refusal time plus `n` seconds (never
earlier than one already recorded), is persisted, and the operation waits for
it and retries once from a fresh read. `FLOOD_WAIT_X`, a `n` beyond `INT_MAX`
and every other error end the operation at once; nothing stays scheduled
after an `Unresolved` finish.

Rows go to the test log only, as `NOTE:` rows, never verdicts; they never
carry the state, the original, the target or the record path, and deadlines
are milliseconds from now:

```
FIXTURE_TRANSACTION: label=<l> op=<none|transition|restore> outcome=<none|equal|written|unresolved|refused> reason=[<r>] reads=<n> writes=<n> waits=<n> pending=<0|1> notBeforeInMs=<ms|none>
FIXTURE_TRANSACTION: label=<l> op=<op> rate=[<error type>] notBeforeInMs=<ms>
FIXTURE_TRANSACTION: label=<l> released busy=<0|1> pending=<0|1> notBeforeInMs=<ms|none>
```

The first is written at construction when the arguments or the record refuse
or a pending record was loaded, and once per finished operation; the second
on each rate refusal, after the deadline is recorded; the third by
`release()` when it stopped an operation or an obligation is pending.

## Lifetime and cleanup

The record is `cWorkingDir() + "test_fixture_obligations/<label>.json"`, in
the live portable copy: `out/Debug/TelegramForcePortable/` normally,
`<dir>/TelegramForcePortable/` under `test-run --portable-root <dir>`. That
copy is repository-ignored, is reused by every later launch on the same marked
copy (SETUP never deletes a marked copy), belongs to the account the fixture
belongs to, and already holds that account's private data. The run's
`EvidenceDir()` would be lost to the next run; a task's `.local/` would let
another task on the same account capture the mutated state as its original;
`work/` and `evidence/` are tracked.

It is one compact JSON object written atomically through `QSaveFile`:
`schema` (`test-fixture-obligation-1`), `label`, `target`, `pending`,
`notBeforeMs` (epoch milliseconds, 0 for none) and, once captured,
`original` (base64 of the complete original state). Loading fails closed: a
wrong schema or label, a non-string target, a non-boolean `pending`, a
negative or non-numeric `notBeforeMs`, an `original` that is not valid base64,
or `pending` without an `original` is `malformed-record`; another target is
`foreign-record`. Both refuse every operation, and the instance never writes
that file.

The record is written at three points only: before each write (`pending` with
the original), on each rate refusal (the deadline), and when a restore's fresh
read equals the original (`pending` cleared). A no-op transition writes no
file, and an `Equal` transition leaves an obligation this instance owns
pending.

The obligation's lifecycle:

- A transition's first read becomes the original only while nothing is
  pending, so a retry read after a refused or applied write never records
  mutated state as the original. The record turns pending before the first
  write is sent; an `authorize` refusal or a failed save before the write
  records nothing and sends nothing.
- Further transitions of the instance that owns the obligation keep the first
  original and only move to the new requested state.
- A record left pending by an earlier launch is loaded at construction
  (`result().pending`, `result().notBefore` from the file) and must be
  restored first: `transition()` refuses `pending-restoration`, and
  `restore()` waits for the recorded deadline before its fresh read (or
  ends `deadline-beyond-bound` at once when it is more than `maxWait`
  away).
- `restore()` clears `pending` only on a fresh read equal to the original,
  directly or after its write. Repeating it on the same instance is a
  fresh-read no-op (`Equal`, no write).
- A resolved record keeps its original but authorizes no restore by a later
  instance (`no-obligation`, no request), so a stale original is never
  written over a newer account state.

Never delete or hand-edit a pending record: it holds the only copy of the
original, and the account stays mutated until it is restored. A malformed or
foreign record blocks its label until a person compares it with the account
and removes it. `test-account-reset` discards the live copy and the record
with it, so restore before resetting; a `--portable-root` sandbox has its own
live copy and record, so a campaign that mutates an account fixture keeps one
root until its record is resolved.

`OpenFixtureTransaction` registers a `Runner::onFinish` release holding the
transaction weakly, so every finish path - completion, skip-to-end, a stage
timeout, the watchdog - calls `release()`. It cancels the deadline timer,
makes every pending request answer and scheduled callback a no-op, turns a
running operation into `Unresolved` `released`, refuses later operations
(`released`) and never writes the record. Destroying the last
`shared_ptr` invalidates the continuations the same way. The release restores
nothing: restoration is an asynchronous stage, and an interrupted one stays
pending in the record for the next launch ([teardown](../teardown.md)).

## Verification

`Test::AppendFixtureTransactionSelfTest(runner)` drives the shipped
transaction through `OpenFixtureTransaction` with a fake local transport and,
except one leg, a fake wall clock. The fake server only stores state, stamps
requests and answers; only the caller's equality is self-test logic
(privacy-rule-shaped literal states: rule order meaningful, people order not,
a rule listing nobody differs from an absent rule). It needs no session,
network, account or fixture secret, writes its records under
`EvidenceDir()/fixture-transaction-self-test/` (removed first, never the live
copy's folder), and every Check is expected to PASS. Its stages, each with
its own label:

- arm: the record folder is fresh, under the evidence directory and not the
  live folder;
- equality (`ft-equal`): an equal read sends no write and writes no record;
- necessary write (`ft-write`, `ft-partial`): read-write-read, the record
  pending with the original when the write is sent, a second transition
  keeping the first original, its restore byte-equal; a partial write ends
  `verify-unequal` with the obligation kept;
- not-before deadline (`ft-rate`, `ft-rate-read`): a `FLOOD_WAIT_13` write
  refusal records refusal + 13000, nothing is sent on an early timer or one
  millisecond before, the retry at the deadline reads first; a refused read
  waits the same way;
- default wall-clock timer (`ft-real-clock`): an empty `FixtureClock` and a
  `FLOOD_WAIT_1`, the one asynchronous stage (about a second);
- equal read at the deadline (`ft-rate-equal`): no second write, pending
  cleared;
- bounded refusal (`ft-exhausted`, `ft-zero`, `ft-bound`, `ft-error`,
  `ft-error-400`): a second refusal, `FLOOD_WAIT_0` twice, `FLOOD_WAIT_3600`,
  `FLOOD_WAIT_X` and a 400 error each end unresolved with the original and
  the latest deadline kept and nothing scheduled;
- teardown (`ft-resume`, `ft-held`, `ft-destroyed`): release while waiting, a
  held read answer delivered after release, a destroyed owner;
- later invocation (`ft-resume`): the deadline rewritten on disk is what a
  new instance reports and waits for; a transition refuses
  `pending-restoration`; the restore is byte-equal; repeating it is a no-op;
  a third instance on the resolved record refuses `no-obligation`;
- exact and unequal originals (`ft-ctl-*`): reordered people restore as
  `Equal`; an absent rule, an extra person, swapped rules and a present empty
  rule over an absent one are written;
- authorization and identity (`ft-auth`, `ft-auth-late`, `ft-malformed`,
  `bad/label`): a refusing or withdrawn `authorize` sends nothing more and
  records no obligation; a foreign or malformed record refuses every
  operation and stays byte for byte; an unsafe label refuses;
- finish ownership (`ft-finish`): a restore left waiting at the end, checked
  from an `onFinish` registered after the transaction's own;
- log secrecy: from the arm offset, `test_log.txt` holds no person id,
  target, state text or base64 original (counts by kind only), while the same
  scan finds the target and the original in the record files, and no
  self-test label has a record in the live folder.

Each falsifier is a Check in the same run: a write on an equal read, a
request before a deadline, a third request after a second refusal, a write
after release, or a sentinel in the log turns that leg FAIL. Proposed, not
part of the self-test: proving a consumer's `.handleFloodErrors()` routing by
delivering `Test::DeliverControlledRpcError(..., 420, "FLOOD_WAIT_<n>")` to a
harmless request on a Test session.

## Related guidance

[Runner gated groups](test_runner.md#gated-groups-and-recovery-stages);
[teardown](../teardown.md); [session gate](test_session_gate.md);
[account fixtures](../account-fixtures.md);
[failure diagnosis](../diagnostics.md).
