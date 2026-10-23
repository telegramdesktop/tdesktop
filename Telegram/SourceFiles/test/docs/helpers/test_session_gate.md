# test_session_gate

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (includes a harness self-test).

Source contract: [test_session_gate.h](../../test_session_gate.h).

## Use when

Every scenario uses the gate automatically. Use its helper to select and restore
a Test session, or its declaration only for explicitly authorized production legs.

## Readings and refusals

The `-testagent` session gate that `Runner::start()` runs before the first
stage of every scenario, self-test packs included (see [the stage
contract](test_runner.md#the-stage-contract)). `ReadSessionGate()` reads,
through public core APIs only, each account's storage index, `sessionExists()`
and `mtp().environment()`, which account the primary window names, which
account owns the session it shows and which is `domain().active()` - never a
phone number, username, name, user id or `uniqueId()`. `ApplySessionGate`
writes the `SESSION_GATE:` account, reading, declaration and decision `NOTE`
rows and refuses with exactly one `fixture gate: session gate` FAIL -
`domain-not-started`, `invalid-declaration`, `non-test-primary`,
`non-test-active` or `selection-unreadable` - after which `RefusedStage` gives
every stage the fixed skip reason, so each writes `N/A` and none of its
callbacks runs; `test-session`, `no-authorized-account`,
`selected-unauthorized` and `declared` pass with `NOTE` rows only.

## Production-leg declaration

`DeclareProductionLegs(runner, { .task = ..., .legs = ... })` is the explicit,
never-default production-leg declaration for a task that itself authorizes
legs on a production-environment account: made once in `SetupScenario` with
non-empty single-line labels (an invalid, repeated or foreign one refuses
without echoing them), it switches no account itself, waives neither an
unstarted domain nor an unreadable selection, and logs `production-leg
declaration task=[...] legs=[...] accepted=<environment>`. When the copy opens
on a Test account, a stage switches to the authorized production account with
`Main::Domain::activate()` after the gate passed and switches back before the
run ends; `activate()` persists the active index, so a copy left on production
refuses the next undeclared scenario.

## Test-session helper

The Test-session helper is the other repair, mutually exclusive with the
declaration (`helper-with-declaration`): `const auto session =
Test::UseTestSession(runner);` in `SetupScenario` only records the request,
and the gate acts on it at start, before any stage. When `domain().active()`,
the primary window's account and the session it shows are already one
authorized Test account it writes `NOTE: SESSION_GATE: test session helper:
switched nothing - ...` and calls no switch; otherwise it selects the
lowest-index authorized Test account, records the restoration obligation first
(the original account held weakly), switches with `Main::Domain::activate()`,
the product's own synchronous account switch, and reads back
`domain().active()`, the window's account, the session the window actually
shows and that session's test mode - only that proof opens the stages. Its
refusals are the gate's one FAIL like the others: `no-test-account` (no
authorized Test account), `helper-selection-split` (the readers do not name
one coherent account, so one restore could not put them all back),
`helper-original-unauthorized` (`Main::Domain::removeRedundantAccounts()` may
drop a session-less original), `switch-not-applied` (the read-back did not
prove the switch; the obligation stays for the finish backstop) and
`invalid-helper` (a request for another runner); a request made after the gate
decided changes nothing and writes a `NOTE`.

## Restoration

`Test::AppendTestSessionRestore(runner, session)` is the scenario's last
stage, after its own teardown: `RestoreTestSession` switches back through the
same route only while a real switch is owed, waits about 1.5 s for the
re-shown window (no wait when nothing was switched), re-reads at the end of
that wait and checks that every reader shows the original again, then calls
the routine again and checks `applied=0` and equality; the `test session
restore` row prints the original, selected and current storage indices, and a
gone original is never equal.

## Finish backstop

The finish backstop: the helper's `Runner::onFinish` registration makes
`Runner::finish()` call `FinishSessionGate` after every release ran, which
restores an obligation a skipped or timed-out restore stage left owed and
verifies - a `NOTE` for a verified no-op, a FAIL under its own name for an
unequal reading that applied nothing. Every restore that switches back, in the
stage or at finish, records its immediate reading and the time of the switch
as the module's outstanding post-switch drain, which only a fresh reading at
the end of that drain clears: the restore stage's check, or the finish
continuation. When none is outstanding - nothing was switched back, or the
restore stage already waited its drain and checked - the run is already
settled and `finish()` goes on at once, as without the helper. When one is
outstanding - `FinishSessionGate` itself switched back (`applied=1`, whatever
its immediate reading, which it Notes), or the scenario watchdog ended the run
while the restore stage was still waiting its drain, so the finish call
applies nothing - `finish()` arms its quit fuse with the deadline extended by
what is left of that drain (counted from the recorded switch, the full 1.5 s
or so after a switch back at finish), yields to the event loop for it, then,
once the drain's recorded end has passed (a timer that arrives earlier only
waits the rest again), `FinishSessionGateAfterDrain` takes a fresh reading
(`source=[finish-settled]`) and checks it and the one taken right after the
switch back equal, so an unequal reading fails the run under its own name
before `SCENARIO_RESULT`; only then come the usual `kFinishDrainDelay` wait,
`SCENARIO_RESULT`, `TEST_COMPLETE` and `Core::Quit()`, once. A drain whose
time is already over but whose fresh reading never ran gets that reading at
once. Like the stage's, that drain mitigates the re-shown window's
chat-background re-cache (the `d->allThreads.isEmpty()` row in [failure
diagnosis](../diagnostics.md)); it does not prove worker quiescence.

## Release binding and side effects

Release binding: a release of a scenario using the helper holds the exact Test
session or object it acts on (a `base::weak_ptr<Main::Session>` taken while
the Test session was shown) and never re-reads `domain().active()` or reuses
the primary window's controller at finish, because the restore stage has
already switched back and `Window::Controller::showAccount` reuses that
controller for the original account; a self-test whose release acts on the
primary window is not packed with the helper without such binding. The switch
is the product's own and carries the product's presence updates:
`Window::Controller::showAccount` calls `Api::Updates::updateOnline` for the
session it shows and, a turn later, for the one it stopped showing, which can
send `account.updateStatus` and, for a session going offline,
`ApiWrap::saveCurrentDraftToCloud()`, as a user's own switch does; the gate
and the helper form no request of their own, and switching before the stages
does not prove the session left behind has no draft.
`Main::Domain::activate()` also persists the active storage index into the
copy (`scheduleWriteAccounts()`); the restore writes it back, and a run killed
before restoring leaves the copy opening on the Test account.

## Verification

Confirm with `AppendSessionGateSelfTest`.

## Related guidance

[Runner contract](test_runner.md); [teardown](../teardown.md); [failure diagnosis](../diagnostics.md).
