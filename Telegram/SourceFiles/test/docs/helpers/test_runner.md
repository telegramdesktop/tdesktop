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

## Related guidance

[Scenario design](../scenarios.md); [overlay example](../overlay-example.md); [teardown](../teardown.md); [failure diagnosis](../diagnostics.md).
