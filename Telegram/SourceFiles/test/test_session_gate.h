/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Usage, refusals and cleanup: docs/helpers/test_session_gate.md.

#include "base/basic_types.h"

#include <QtCore/QString>

#include <memory>
#include <optional>
#include <vector>

namespace MTP {
enum class Environment : uchar;
} // namespace MTP

namespace Test {

class Runner;
struct Stage;

// The -testagent session gate: no stage of a scenario acts as an authorized
// account that is not on the Test environment, unless the scenario declares
// by name that its task authorized legs on such an account.
//
// Why it exists. Scenarios and shared helpers act on
// Core::App().domain().active() or Core::App().activePrimaryWindow()
// (Runner::waitForSessionReady, Runner::waitForChatsLoaded, test_rpc_fixture,
// test_rpc_retry, test_web_cache, test_notify_override and most self-tests),
// and nothing checked which account that is. A marked live copy can hold an
// authorized production-environment account beside the Test one, with the
// primary window opening on it: run 1 of
// 2026/10/15/accept-payment-free-links-and-import-their-token (its
// work/test.md) read a Production account at storage index 0, shown by the
// primary window, beside the Test account at index 1, and only that task's
// own overlay gate kept its requests off the production account. Fixture
// chats of earlier campaigns already sit on such an account.
//
// When it runs. Runner::start() calls ApplySessionGate(this,
// ReadSessionGate()) once, after SCENARIO_START and the watchdog are armed
// and before Runner::beginStage() runs the first stage's |run| (which happens
// synchronously inside start()), so it covers every scenario, self-test packs
// included. It is a startup gate, not a continuous account lock: code that
// SetupScenario itself executes is not a stage and is not gated, so
// SetupScenario only appends stages, registers releases and configures the
// gate, and no stage may switch the primary window or domain().active() to
// another account behind it. The one account switch the harness makes is
// the Test-session helper's, inside the gate, before any stage (below).
//
// What it reads (ReadSessionGate). Public core APIs only: whether
// Main::Domain::started(); for every entry of Main::Domain::accounts() its
// storage index, Main::Account::sessionExists() (authorized) and
// mtp().environment(); which account Core::App().activePrimaryWindow() names
// (Window::Controller::id().account), which account owns the session that
// window's SessionController actually shows and whether that session
// isTestMode(); and which account is domain().active(), read only while the
// domain is started. Pointers become storage indices and nothing else is
// kept, so no field can carry a phone number, username, name, user id or
// Main::Session::uniqueId().
//
// Rows. Test-log rows only, never the app log; indices and counts print as
// ordinary integers, like the runner's own rows:
//   NOTE: SESSION_GATE: account index=<i> authorized=<0|1>
//     environment=<Production|Test> primary=<0|1> active=<0|1>
// one per account, before any TEST_STEP (an environment other than those
// two prints unknown-<n>); then
//   NOTE: SESSION_GATE: reading domainStarted=<0|1> accounts=<n>
//     authorized=<n> primaryIndex=<i|none> primarySessionIndex=<i|none>
//     primarySessionTest=<0|1> activeIndex=<i|none>
// then, only for one valid declaration,
//   NOTE: SESSION_GATE: production-leg declaration task=[<task>]
//     legs=[<legs>] accepted=<environment|none> index=<i|none>
// and the decision. A pass writes
//   NOTE: SESSION_GATE: decision pass case=<case>: <details>
// and no verdict row, so it changes no result, count or SCENARIO_RESULT. A
// refusal writes exactly one
//   TEST_RESULT: FAIL: fixture gate: session gate - case=<case>: <details>
// and Runner::start() hands every stage the skip reason
//   fixture gate: session gate refused the scenario (case=<case>)
// through RefusedStage, so the existing Runner::beginStage() loop writes one
// TEST_RESULT: N/A: <stage> - <reason> row per stage, runs none of their
// callbacks and reaches finish() and TEST_COMPLETE in the same turn. The
// Test-session helper adds the rows listed with it below.
//
// Refusals:
// - domain-not-started: the domain is not started (a local passcode locks
//   the copy), so no environment can be read; no declaration waives it;
// - invalid-declaration: Test::DeclareProductionLegs was made more than
//   once, for another runner, or with an empty or multi-line label; the
//   labels are not echoed;
// - non-test-primary: the primary window names, or actually shows the
//   session of, an authorized account whose environment is not Test;
// - non-test-active: domain().active() is such an account while the primary
//   window does not show it, and every stage or helper reading
//   domain().active() would still act as it;
// - selection-unreadable: an authorized session may be selected, but the
//   primary window's reading is missing or contradicts itself (no window or
//   no window account, a shown session of another account, a session whose
//   test mode disagrees with its account's environment) or domain().active()
//   names no account, so what the stages would act as is unknown; it fails
//   closed and no declaration waives it;
// - helper-with-declaration, invalid-helper, no-test-account,
//   helper-selection-split, helper-original-unauthorized and
//   switch-not-applied: the Test-session helper's own refusals (below).
// Passes:
// - test-session: every authorized account the primary window, its shown
//   session and domain().active() select is on the Test environment;
// - no-authorized-account: the started domain holds no authorized account
//   (an account-free launch);
// - selected-unauthorized: no reader selects an authorized account although
//   another account is authorized; an account no reader selects is not what
//   a stage acts as, whatever its environment;
// - declared: one valid production-leg declaration accepts the readable
//   non-Test environment of a selected authorized account.
// Any environment other than MTP::Environment::Test counts as non-Test.

// One account as the gate read it: its Main::Domain storage index, whether
// it is authorized (Main::Account::sessionExists()) and its MTP environment.
struct SessionGateAccount {
	int index = -1;
	bool authorized = false;
	MTP::Environment environment = MTP::Environment();
};

// Everything the gate decides from. The indices are storage indices of
// |accounts|, -1 when that reader selects no account of the domain:
// |primaryIndex| is the account activePrimaryWindow() names,
// |primarySessionIndex| the account owning the session its SessionController
// shows, |primarySessionTest| that session's isTestMode() and |activeIndex|
// domain().active(). A domain that is not started reads |domainStarted|
// false and nothing else.
struct SessionGateReading {
	std::vector<SessionGateAccount> accounts;
	int primaryIndex = -1;
	int primarySessionIndex = -1;
	int activeIndex = -1;
	bool domainStarted = false;
	bool primarySessionTest = false;
};

// The real reading, taken at the moment of the call.
[[nodiscard]] SessionGateReading ReadSessionGate();

// The explicit production-leg declaration: |task| is the id of the task that
// authorizes legs on a production-environment account, |legs| names those
// legs. Both are the scenario author's public text and must never carry a
// phone number, username, name or user id; each must be non-empty and one
// line, and an invalid one is refused without being echoed.
struct ProductionLegs {
	QString task;
	QString legs;
};

// Declares, from SetupScenario and only when the task itself explicitly
// authorizes legs on a production-environment account (a string cannot
// establish that authorization), that the scenario may run on such a
// session. Never the default. Made once and before Runner::start(); a second
// declaration or one made for another runner refuses the scenario
// (invalid-declaration), and one made after the gate decided changes
// nothing and only writes a NOTE. A valid declaration lets an authorized
// non-Test selected session run, with the declaration row naming the
// environment it accepted; it switches no account and does not waive an
// unreadable domain or an unreadable selection. It is mutually exclusive
// with Test::UseTestSession: a scenario requesting both is refused
// (helper-with-declaration).
void DeclareProductionLegs(not_null<Runner*> runner, ProductionLegs legs);

// The Test-session helper, the repair for a copy whose primary window
// opens on another account. A scenario opts in from SetupScenario:
//   const auto session = Test::UseTestSession(runner);
//   ... the scenario's stages and its own teardown ...
//   Test::AppendTestSessionRestore(runner, session); // the last stage
//
// The switch. UseTestSession only records the request; ApplySessionGate
// acts on it at start, before any stage, from the reading the account
// rows print:
// - when domain().active(), the primary window's account and the session
//   that window shows are one authorized Test account, it switches nothing
//   and says so, and no product switch is called;
// - otherwise it selects the lowest-index authorized Test-environment
//   account, refused as no-test-account when there is none;
// - it refuses before switching when the original could not be restored
//   as a whole: helper-selection-split when domain().active(), the
//   window's account and its shown session do not name one coherent
//   account (one restore could not put every reader back), and
//   helper-original-unauthorized when the original active account is not
//   authorized (Main::Domain::removeRedundantAccounts() may drop such an
//   account once another one is active);
// - otherwise it records the restoration obligation first (the original
//   account held weakly, its storage index and environment), switches
//   with Main::Domain::activate(), the product's synchronous account switch,
//   and reads back domain().active(), the window's account, the session the
//   window actually shows and that session's test mode; only a read-back
//   that shows the selected Test session in all of them opens the stages,
//   anything else is switch-not-applied, and the obligation stays so the
//   finish backstop switches back.
// helper-with-declaration refuses a scenario that also declared production
// legs, invalid-helper one whose request named another runner. Each helper
// refusal is the gate's one FAIL, so every stage is skipped. A request made
// after the gate decided changes nothing and only writes a NOTE.
//
// What the switch carries. It is the product's own switch, so it carries
// the product's presence traffic: Window::Controller::showAccount calls
// Api::Updates::updateOnline for the session it shows and, a turn later,
// for the one it stopped showing, which can send account.updateStatus and,
// for a session going offline, ApiWrap::saveCurrentDraftToCloud(), as a
// user's own switch does. The helper forms no request of its own, and
// switching before the stages does not prove the session it leaves has no
// draft.
// Main::Domain::activate() also persists the active storage index into the
// copy (scheduleWriteAccounts()); the restore writes it back, and a run
// killed before restoring leaves the copy opening on the Test account.
//
// Releases. The normal restore is the last stage, so it runs before
// Runner::finish() runs the onFinish releases: a release of a scenario
// using the helper must hold the exact Test session or object it acts on
// (a base::weak_ptr<Main::Session> taken while the Test session was shown)
// and never re-read domain().active() or reuse the primary window's
// controller at finish, since Window::Controller::showAccount reuses that
// controller for the restored account. A self-test whose release acts on
// the primary window is not packed with the helper without such binding.
//
// Rows (test log only):
//   NOTE: SESSION_GATE: test session helper: switched nothing - ...
//   NOTE: SESSION_GATE: test session helper: switched index=<o>
//     environment=<E> -> index=<t> environment=Test through
//     Main::Domain::activate(); read back primaryIndex=<i|none>
//     primarySessionIndex=<i|none> primarySessionTest=<0|1>
//     activeIndex=<i|none> proven=<0|1>
// and TestSessionRestoreText's text in the restore stage's two checks, in
// the finish verification's NOTE or FAIL and, while a switch back's
// post-switch drain is outstanding at finish, in its check at the end of
// that drain.

// The helper's handle, filled when the gate decides: |decided| once it
// acted on the request, |refusal| the case name of a helper refusal,
// |originalIndex| and |selectedIndex| the storage indices of the original
// active account and of the Test account it selected (equal when it
// switched nothing), |switchCalled| that Main::Domain::activate() was
// called, |proven| that the read-back showed the selected Test session.
struct TestSessionSwitch {
	QString refusal;
	int originalIndex = -1;
	int selectedIndex = -1;
	bool decided = false;
	bool switchedNothing = false;
	bool switchCalled = false;
	bool proven = false;
};

// Records, from SetupScenario, that the gate is to show an authorized Test
// session in the primary window before the stages run. Calling it again
// returns the same handle; never null.
[[nodiscard]] std::shared_ptr<const TestSessionSwitch> UseTestSession(
	not_null<Runner*> runner);

// One restore or verification of the helper's original account. |owed|
// is whether the restoration obligation was outstanding when the call
// began, |applied| that this call switched back (Main::Domain::activate()
// on the original), |captured| that the helper captured an original at
// all. The rest is read after the call: the storage indices of
// domain().active() (the current one), of the primary window's account
// and of the account owning the session it shows, that session's test
// mode and the active account's environment. |equal| is true only when
// the original account still lives and every one of those readers shows
// it, still authorized, on its captured environment; a gone original
// (|originalGone|) is never equal.
struct TestSessionRestore {
	QString source;
	std::optional<MTP::Environment> activeEnvironment;
	MTP::Environment originalEnvironment = MTP::Environment();
	int originalIndex = -1;
	int selectedIndex = -1;
	int activeIndex = -1;
	int primaryIndex = -1;
	int primarySessionIndex = -1;
	bool captured = false;
	bool owed = false;
	bool applied = false;
	bool originalGone = false;
	bool primarySessionTest = false;
	bool equal = false;
};

// "test session restore source=[<source>] captured=<0|1> owed=<0|1>
// applied=<0|1> originalIndex=<i|none> selectedIndex=<i|none>
// activeIndex=<i|none> primaryIndex=<i|none> primarySessionIndex=<i|none>
// primarySessionTest=<0|1> activeEnvironment=<E|none>
// originalEnvironment=<E|none> originalGone=<0|1> equal=<0|1>".
[[nodiscard]] QString TestSessionRestoreText(
	const TestSessionRestore &restore);

// The one restore routine of the teardown stage and the finish backstop.
// Only while a real switch's obligation is owed does it switch back,
// through the same synchronous product route and a weak pointer to the
// original (a dead original is never touched); it then re-reads, and it
// consumes the obligation only when that reading is equal. A call that
// switched back also records that reading and the time of the switch as
// the module's outstanding post-switch drain, which only a fresh reading
// at the end of the drain clears (the restore stage's check or
// FinishSessionGateAfterDrain), so Runner::finish() still waits what is
// left of it when it ended the run during the stage's wait. Every other
// call applies nothing and only reads and compares. It writes no row: the
// caller judges the reading.
[[nodiscard]] TestSessionRestore RestoreTestSession(const QString &source);

// Appends the scenario's last stage, after its own teardown released every
// account- and window-bound object: RestoreTestSession, then a bounded
// post-switch drain of about 1.5 s for the re-shown window (none when
// nothing was switched), then a fresh reading at the end of that wait,
// checked equal, which clears the outstanding drain, then
// RestoreTestSession again, checked applied=0 and equal. |switched| is the
// handle UseTestSession returned. The drain is the module's, not the
// stage's: when the scenario watchdog finishes the runner during the wait,
// FinishSessionGate finds it outstanding and Runner::finish() waits the
// rest of it and checks a fresh reading before completing. The drain
// mitigates the chat-background re-cache a re-shown window starts (the
// d->allThreads.isEmpty() row of README.md's "Failure diagnosis"); it does
// not prove worker quiescence.
void AppendTestSessionRestore(
	not_null<Runner*> runner,
	std::shared_ptr<const TestSessionSwitch> switched);

// Runner::start()'s gate over |reading| and the declaration or the
// Test-session helper requested for |runner|: writes the rows above, acts
// on the helper's request (switching, when needed, and registering the
// helper's Runner::onFinish verification once it captured an original) and
// returns the skip reason of a refusal, or an empty string when the
// scenario may run. Called once per runner.
[[nodiscard]] QString ApplySessionGate(
	not_null<Runner*> runner,
	const SessionGateReading &reading);

// Runner::finish()'s one call after its release loop, so it runs after
// every onFinish callback registered before or during finish(). When the
// helper's onFinish registration for |runner| marked it due, it calls
// RestoreTestSession once more: that restores an obligation a skipped or
// timed-out restore stage left owed, so it is the backstop of an already
// failed run. A verified no-op (applied=0, equal=1) writes a NOTE, an
// unequal reading that applied nothing one FAIL under its own name, never
// another fixture gate row. It returns how long Runner::finish() must wait
// before its completion sequence:
// - zero without the helper and whenever no switch back's post-switch
//   drain is outstanding: nothing was switched back, or the restore stage
//   already waited its drain and checked a fresh reading. This is the
//   already-settled case, and the runner goes on at once, as it does
//   without the helper;
// - what is left of the outstanding drain, counted from the recorded switch
//   back, when one is outstanding: this call switched back (applied=1,
//   whatever its immediate reading, which it Notes; the full drain of
//   about 1.5 s), or the scenario watchdog ended the run while the restore
//   stage was still waiting the drain of its own switch back, so this call
//   applies nothing and still Notes or fails its reading. Runner::finish()
//   then arms its quit fuse with the deadline extended by that wait, yields
//   to the event loop for it, calls FinishSessionGateAfterDrain, waits again
//   for as long as that call answers some of the recorded drain is still
//   left, and only then runs its own kFinishDrainDelay wait,
//   SCENARIO_RESULT, Complete() and Core::Quit(), once. A drain whose time
//   is already over but whose fresh reading never ran gets that reading
//   here, at once, and zero.
// Like the stage's, the drain mitigates the re-shown window's
// chat-background re-cache (the d->allThreads.isEmpty() row of README.md's
// "Failure diagnosis"); it does not prove worker quiescence.
[[nodiscard]] crl::time FinishSessionGate(not_null<Runner*> runner);

// The fresh verification that ends an outstanding post-switch drain at
// finish: Runner::finish() calls it at the end of the wait FinishSessionGate
// returned for |runner|. It first checks the drain's own recorded deadline,
// because the timer ending that wait may arrive before it: while some of
// the drain is left it reads nothing, writes no row, keeps the drain and
// returns what is left, which Runner::finish() waits before calling it
// again. Once the drain is over it takes a reading that applies nothing
// (source=[finish-settled]) and writes one Check, before SCENARIO_RESULT
// counts the failures, that both the reading taken right after the switch
// back (the finish backstop's or the interrupted restore stage's, named by
// its source) and this one are equal; it clears the drain and returns
// zero. Does nothing and returns zero when no drain is outstanding.
[[nodiscard]] crl::time FinishSessionGateAfterDrain(not_null<Runner*> runner);

// |stage| with its skipReason replaced by one returning |reason| (a fixed
// fallback when |reason| is empty, so the stage is always skipped). The
// original skipReason is dropped unread and never runs; the name, |run|,
// |until|, |then| and the timeout are kept, and the runner's skip contract
// excludes them.
[[nodiscard]] Stage RefusedStage(Stage stage, const QString &reason);

// The gate deciding from controlled readings, never from the real accounts:
// it calls no ReadSessionGate, ApplySessionGate, DeclareProductionLegs,
// UseTestSession or RestoreTestSession, switches no account, touches no
// api(), mtp() or wallet code and sends no request, so it needs no session,
// network or MTP instance and builds no widget.
// - A Production primary reading refuses as non-test-primary, naming the
//   Production environment and the fixed skip reason.
// - Two stages behind that controlled refusal are made through RefusedStage,
//   each with counters on its original skipReason, |run|, |until| and |then|
//   and a never-ready |until| under a one-second timeout: if the skip did not
//   take, the stage fails by timeout instead of hanging the run. The next
//   stage checks that SkippedCount() grew by exactly two and that no
//   original callback was entered.
// - A Test primary passes (beside an authorized Production account that no
//   reader selects), an account-free reading passes, and the declaration
//   passes and names the environment it accepted.
// - A non-Test domain().active() refuses though the window shows Test, a
//   window whose shown session is Production refuses, missing or
//   self-contradicting readings fail closed, an unstarted domain refuses
//   with or without a declaration, and an empty, multi-line, repeated or
//   foreign declaration refuses without echoing its labels.
// - The account rows carry the index, authorization, environment and window
//   roles only, in the fixed format.
// - The Test-session helper decides from the reading before it acts: a
//   coherent Test primary switches nothing, a Production primary selects
//   the lowest-index authorized Test account, and no Test account, split
//   readers, an unauthorized original, a declaration beside the helper, a
//   foreign runner and an unstarted domain refuse by name.
// - The switch and restore read-backs discriminate: a switch whose
//   domain().active() changed while the window did not, or whose window
//   shows a non-Test session, is not proven; a restore is equal only when
//   every reader shows the original again, and never for a lost original.
//   These are the predicates alone; nothing is switched.
// Every check prints its reading and verdict on both verdicts. On a healthy
// harness whose real gate passed, its run carries exactly two TEST_RESULT:
// N/A rows, PASS rows only otherwise, and no FAIL.
void AppendSessionGateSelfTest(not_null<Runner*> runner);

} // namespace Test
