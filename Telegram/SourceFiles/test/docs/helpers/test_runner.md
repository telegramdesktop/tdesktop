# test_runner

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_runner.h](../../test_runner.h).

## Use when

Stages, bounded readiness and exact-object actions. Read the contract below and the source header before use.

## Contract and usage

Stages, bounded waits, exact-widget actions, prepared capture/inspection
(`captureAndInspect` takes an optional trailing `skipReason` for its stage),
first-class gated skips (`skipReason`), `onFinish` release hook (and its
finish-release self-test), watchdog (`TDESKTOP_TEST_WATCHDOG` in seconds) and
termination.

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

## Related guidance

[Scenario design](../scenarios.md); [overlay example](../overlay-example.md); [teardown](../teardown.md); [failure diagnosis](../diagnostics.md).
