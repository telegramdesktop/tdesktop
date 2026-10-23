# test_runner

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_runner.h](../../test_runner.h).

## Use when

Stages, gated groups, bounded readiness and exact-object actions. Read the contract below and the source header before use.

## Contract and usage

Stages, bounded waits, exact-widget actions, prepared capture/inspection
(`captureAndInspect` takes an optional trailing `skipReason` for its stage),
first-class gated skips (`skipReason`), an inherited fixture gate for a
composed group of stages (`addGated`) with explicitly declared restoration
stages (`addRecovery`, see
[gated groups and recovery stages](#gated-groups-and-recovery-stages)),
`onFinish` release hook (and its finish-release self-test), watchdog
(`TDESKTOP_TEST_WATCHDOG` in seconds) and termination.

## The stage contract

`Runner` executes each `Stage` as three distinct operations:

| Member | Contract | Must not do |
| --- | --- | --- |
| `run` | One action whose prerequisites were established by an earlier stage. | Dereference an object that this stage is supposed to wait for. |
| `until` | A pure, repeatable readiness observation. | Click, mutate state, emit a PASS/FAIL, or encode the expected product result. The one documented exception is the `Test::ForceWindowActive` re-assertion in the activation row in [failure diagnosis](../diagnostics.md). |
| `then` | Assertions and the next action after readiness succeeded. | Resolve an unguarded replacement for the object that readiness accepted. |
| `timeoutDetails` | Return the latest observed values and identities. | Repeat only “not ready”; omit the values needed to diagnose why. |
| `skipReason` | Return why this stage does not apply, or an empty string when it does. A non-empty reason writes `TEST_RESULT: N/A: <stage> - <reason>`, skips `run`, `until` and `then`, and moves on in the same turn. | Decide the product outcome. A gate that reads a measurement instead of a precondition turns a would-be FAIL into a silent N/A. |

A stage timeout is a harness failure unless the readiness condition itself is
the behavior under test. Keep expected geometry, text, pixels, counts, and
other falsifiable outcomes in `then` or `captureAndInspect`, not in `until`.
Otherwise a real product mismatch is mislabeled as an opaque timeout.

Use `Runner::actOnWidget` when an action depends on a widget that may appear
asynchronously. It polls the resolver and optional readiness predicate, keeps
a `QPointer` to the exact accepted object, and invokes the action once. This
removes both the eager-`.run` crash and the “resolve again in `.then`” race.
The same rule is built into the harness's widget-carrying readings:
`Test::BoxShellButtons`, `Test::WindowMappedCapture`,
`Test::PaintingLayerRootResult`, `Test::ToastSubtreeReading` and
`Test::ContextMenuReading` hold `QPointer<QWidget>`, so a reading kept
across a stage boundary answers `matched()` / `resolved()` false (a
context-menu reading's `menu` reads null) once its subject is destroyed and
still prints its counts, labels, identity and refusal; format a
`WidgetDescription` of the pointer while the reading is still matched,
because there is nothing to format afterwards.

The generic startup waits are opt-in:

- `waitEvent("launch_finished")` is the normal first gate.
- `waitForSessionReady()` is needed only when the scenario consumes a
  session.
- `waitForChatsLoaded()` is needed only when the tested setup consumes the
  chats list; it is non-fatal and records whether loading completed.
- `waitForChatsLoadedStrict()` is appropriate only when chats loading is part
  of the required fixture or result.

Do not put all three waits in every scenario preamble. An injected box,
helper oracle, or direct production seam often needs none of the account or
chats gates.

Before the first stage begins, `Runner::start()` runs the session gate
([`test_session_gate.h`](../../test_session_gate.h)) for every scenario, self-test packs included. It
writes one row per account,
`NOTE: SESSION_GATE: account index=<i> authorized=<0|1> environment=<Production|Test> primary=<0|1> active=<0|1>`,
then a `reading` row, and on a pass a `decision pass` row and nothing else:
it changes no result when every authorized account that the primary window,
its shown session and `domain().active()` select is on the Test environment,
or when no account is authorized. When one of them selects an authorized
account on another environment, it writes exactly one
`TEST_RESULT: FAIL: fixture gate: session gate - case=...` row naming that
environment before any `TEST_STEP`, and every stage then reads
`TEST_RESULT: N/A: <stage> - fixture gate: session gate refused the scenario (case=<case>)`
with none of its callbacks run; the run still reaches `TEST_COMPLETE`. The
gate decides once, at start, and is not a continuous
account lock: code that `SetupScenario` itself executes is not a stage and is
not gated, so `SetupScenario` only appends stages, registers releases and
configures the gate, and no stage may switch the primary window or
`domain().active()` to another account behind it. The one account switch
the harness makes is the Test-session helper's (`Test::UseTestSession`),
inside the gate and before any stage, undone by its last stage.

## Gated groups and recovery stages

`Runner::addGated(gate, compose)` shares one fixture prerequisite with a
composed group of stages. It calls `compose` once, synchronously, before
`start()`, and every stage appended while it runs inherits `gate`: stages
added through `add`, the sugar waits, `actOnWidget`, `captureWidget`,
`captureAndInspect`, and every stage that an `Append*` or `Capture*` helper
given the runner appends, because all of them append through
`Runner::add()`. A helper needs no skip parameter of its own to be gated.

When an inherited stage begins, `Runner::beginStage()` reads its reason in
this order, and the first non-empty answer wins:

| Order | Reason | Read when |
| --- | --- | --- |
| 1 | The session gate's refusal | Always: `Runner::start()` replaces every stage's reason through `RefusedStage`, so on a refusal nothing below is read. |
| 2 | The outermost open group's gate | The session gate passed. |
| 3 | Each nested group's gate, outer to inner | Every outer gate answered empty. |
| 4 | The stage's own `skipReason` (for `captureAndInspect`, its trailing parameter) | Every group gate answered empty. |

A non-empty answer writes `TEST_RESULT: N/A: <stage> - <reason>`, counts in
`SkippedCount()` and in `SCENARIO_RESULT`'s skipped clause, and skips `run`,
`until` and `then`: the stage resolves no widget, polls no readiness,
dispatches no action and takes no capture. The gates after it and the
stage's own `skipReason` are not called at all.

Each group latches its first refusal. Its gate is read again at every stage
begin while it answers empty; its first non-empty answer is then kept for
every later stage of that group. A gate that later answers another reason
(an incidental `target does not exist`) or empty again (a restoration that
cleared the overlay's state) changes nothing, so every row of the group
names the first useful refusal. A nested group latches its own answer and
is never read while an outer group refuses.

The gate is read when each stage begins, not at composition, so the
prerequisite must be settled by an earlier stage: await an asynchronous
answer (a fixture write, a media gate) in that stage's `until` and set the
state the gate reads in its `then`. A gate reading a request that is still
running answers empty and lets the dependent stage run.

Not inherited: stages appended after `compose` returns, `addRecovery`
stages, `onFinish` callbacks registered during `compose`, and any work
`compose` does other than appending. Building a widget, arming a timer or
sending a request inside `compose` happens at composition whatever the gate
later answers: put such work in a gated stage's `run`, and release what
composition acquired through `onFinish`.

`Runner::addRecovery(stage)` appends `stage` with only its own
`skipReason`, even inside an open group. That reason is the restoration's
own recovery policy, typically whether an obligation is owed; recovery is
never inferred from a skipped stage. Use it for restoration between the
legs of a campaign-wide group: leg 1's restoration runs after leg 1's
prerequisite refused, while leg 2's stages still read leg 1's first refusal.
Outside a group `addRecovery` behaves like `add` and keeps the stage
independent if a later edit moves it into a group. A restoration helper that
appends through `add`, `Test::AppendTestSessionRestore` among them, inherits
the gate when called inside `compose`: call it after the group. The session
gate's refusal still skips a recovery stage, as it skips every stage.

The appended-helper pitfall: a per-stage `skipReason` reaches only the stage
it is set on. Setting it on an overlay's own wrapper stages, or passing it
to `captureAndInspect` or `CapturePopupMenu`, never reaches the stages that
`actOnWidget` or another helper appends. In Run 2 of
`2026/10/17/open-ones-own-qr-box-without-a-username-with-temporary-only-codes`
a genuine `FLOOD_WAIT_13` refused a privacy fixture write; the overlay's
wrapper stages read N/A, but its direct `actOnWidget` steps ran anyway, a
later reopen timed out, and that timeout ended the run before the ordinary
restoration stages. The overlay's gate was also a plain string that later
stages overwrote, so the first refusal was lost. Compose such stages in
`addGated` instead.

`onFinish` is unaffected: gating skips stages, never releases. A group
refused to the end of the run reaches `finish()` from the `beginStage()`
skip loop, and the `onFinish` callbacks run once, as on every other finish
path. A genuine timeout of an applicable gated stage, one whose gate
answered empty, is still a `stage timed out` FAIL.

A minimal sketch; `ApplyPremise`, `PremiseSettled`, `PremiseHolds`,
`PremiseText`, `RestorePremise`, `PremiseRestored` and `AppendTargetChecks`
stand for the overlay's own code:

```cpp
struct State {
	QString gate;
	bool owed = false;
};
const auto state = std::make_shared<State>();
runner->add({
	.name = u"fixture: apply the premise"_q,
	.run = [=] { state->owed = ApplyPremise(); },
	.until = [] { return PremiseSettled(); },
	.then = [=] {
		const auto holds = PremiseHolds();
		Test::Check(holds, u"fixture gate: premise holds"_q, PremiseText());
		if (!holds) {
			state->gate = u"fixture gate: premise not established"_q;
		}
	},
});
runner->addGated([=] { return state->gate; }, [&] {
	runner->actOnWidget(
		u"open the target"_q,
		[] { return Test::ReadLiveWidget(u"task.target"_q).widget; },
		[](QWidget *widget) { Test::Click(widget); });
	AppendTargetChecks(runner); // Its stages inherit the gate too.
	runner->addRecovery({
		.name = u"restore the premise"_q,
		.skipReason = [=] {
			return state->owed
				? QString()
				: u"applies=0: nothing to restore"_q;
		},
		.run = [] { RestorePremise(); },
		.until = [] { return PremiseRestored(); },
	});
});
```

A server fixture such as privacy rules is applied and restored through the
[fixture transaction](test_fixture_transaction.md#minimal-use), whose page
shows the same shape with a real read-before-write transaction. The
behavior is measured by `Test::AppendStageGroupGateSelfTest`
([its page](test_gated_stage.md#appendstagegroupgateselftest)).

## Related guidance

[Scenario design](../scenarios.md); [overlay example](../overlay-example.md); [teardown](../teardown.md); [failure diagnosis](../diagnostics.md).
