# test_gated_stage

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_gated_stage.h](../../test_gated_stage.h).

## Use when

Self-tests for explicit and inherited gated skips. Read the contract below and the source header before use.

## Contract and usage

The first-class gated skip's own self-test: a stage whose `skipReason` returns
a reason, writing one `TEST_RESULT: N/A:` row and skipping `run`, `until` and
`then` without waiting - its never-ready `until` under a one-second timeout is
the falsifier - beside a stage whose gate returns an empty string and runs
normally in the tick that begins it.

## AppendStageGroupGateSelfTest

`Test::AppendStageGroupGateSelfTest(runner, path)` measures the inherited
group gate of `Runner::addGated` and the explicit recovery stages of
`Runner::addRecovery`
([gated groups and recovery stages](test_runner.md#gated-groups-and-recovery-stages)).
It protects four behaviors: every stage appended inside a refused group,
helper stages included, writes the group's first refusal before it resolves,
polls, acts or captures anything; a later, cleared, nested or own reason
never replaces that first refusal; restoration declared with `addRecovery`
or composed after the group runs on its own policy; and the `onFinish`
release still runs exactly once on every finish path.

Fixture and environment. The overlay calls
`runner->waitEvent(u"launch_finished"_q)` first. The arm stage builds one
local painted `Ui::RpWidget` (opaque, `st::windowBg` with one
`st::attentionButtonFg` band, sized from `st::defaultActiveButton.height`)
as a child of `Core::App().activePrimaryWindow()`, records the
`test_log.txt` byte offset, `SkippedCount()` and `FailureCount()`, and checks
`fixture gate: stage-group self-test fixture built`. Nothing else is needed:
no session, network, fixture secret or account, so an account-free
`--portable-root` sandbox is enough. The self-test uses no fixture
transaction.

Composition, in order:

- A prerequisite stage outside any group whose `then` refuses by
  construction: the gate becomes `fixture gate: stage-group self-test
  prerequisite refused by construction`.
- The refused group, `addGated` over that gate. While composing it arms a
  5 ms `base::Timer`, a resource a helper acquires while appending. It
  appends the shared seven-stage dependent body with a null resolver and
  1 s timeouts: an ordinary stage with a never-ready `until`, an ordinary
  stage with its own `skipReason`, `actOnWidget`, `captureWidget`,
  `captureAndInspect` with its trailing `skipReason`, and a gate-unaware
  helper's `add` and `actOnWidget`. Then an `addRecovery` stage that sets a
  later reason (`later incidental refusal: target does not exist`), an
  ordinary stage, an `addRecovery` stage that clears the gate, an ordinary
  stage, and a nested `addGated` group with one `actOnWidget`.
- After the group, two restoration stages with their own `skipReason`: one
  empty (runs), one `applies=0: no restoration owed by construction` (N/A).
- The healthy controls: the same seven-stage body inside an `addGated`
  group whose gate always answers empty, then ungated, both resolving the
  local widget with readiness true and the default stage timeout.
- One stage of Checks, then the ending `path` selects.

Every stage and capture name carries a file-safe group token,
`group-refused`, `group-open`, `group-ungated` or `group-ending`, because
capture names become screenshot file names; the captures are
`<token>-capture` and `<token>-inspect`, written as stages
`capture painted widget: <token>-capture`, for example.

Counters and the log oracle. Every callback of the dependent body counts
into its group's counters: `runs`, `untilPolls`, `thens`, `ownSkipReads`,
`resolves`, `readies`, `actions`, `captureResolves`, `captureReadies`,
`inspectResolves`, `inspectReadies`, `inspects`, `helperRuns`,
`helperResolves` and `helperActions`. The Checks stage writes, with the full
readings on both verdicts:

- `the refused group resolved, polled, acted and captured nothing`: every
  `group-refused` counter 0, and `afterLater`, `afterCleared` and
  `innerGateReads` 0.
- `the recovery stages ran on their own policy`: `recoveryLater=1
  recoveryCleared=1 independentRuns=1 notOwedRuns=0`.
- `the skipped and failure counters moved by the composed rows alone`:
  `skippedDelta=11` (ten refused-group rows and the not-owed restoration)
  and `failuresDelta=0`.
- `every refused-group row carries the first refusal`: reading
  `test_log.txt` back from the arm offset, exactly 10 `TEST_RESULT: N/A:`
  rows containing `group-refused`, each ending
  ` - fixture gate: stage-group self-test prerequisite refused by construction`,
  no N/A row ending with the later, inner or own reason, and no
  `stage timed out` row; the row names are listed when it fails.
- `the restoration not owed wrote its own reason`: one row ending
  ` - applies=0: no restoration owed by construction`.
- The healthy gated and ungated controls, each: `runs=1`, `thens=2`,
  `untilPolls>=1`, `ownSkipReads=2`, `actions=1`, `resolves>=1`,
  `captureResolves>=1`, `inspects=1`, `helperRuns=1`, `helperActions=1`; the
  open group's gate was read once per stage begin and never latched while
  empty (`openGateReads=7`). Each control also writes two `SCREENSHOT:`
  rows and an `inspected a painted frame` PASS; the screenshots prove only
  that the capture path ran.

The falsifier is built in: a refused-group stage whose skip did not take
meets a never-ready `until` or the null resolver under a one-second
timeout, so the run ends with a `stage timed out: ... group-refused ...`
FAIL instead of reaching the Checks stage.

One Runner finishes once, so `StageGroupGatePath` selects the ending:

| Path | Ending | Expected verdict |
| --- | --- | --- |
| `Complete` | One plain stage noting `ending path=complete`. | No FAIL. |
| `SkippedToEnd` | A final refused group (`fixture gate: stage-group self-test ending group refused by construction`) of two never-ready `group-ending` stages: two more N/A rows, and `finish()` comes from the `beginStage()` skip loop. | No FAIL; the release row's `ending={...}` counters all 0. |
| `Timeout` | An open final group with one never-ready `group-ending applicable timeout` stage and a 500 ms timeout. | Exactly one `stage timed out` FAIL. |
| `Watchdog` | The same stage, named `... applicable watchdog`, with the default stage timeout; run with `TDESKTOP_TEST_WATCHDOG` of a few seconds. | Exactly one `scenario watchdog` FAIL. |

Teardown. The self-test registers its `Runner::onFinish` release before any
stage. It cancels the composition-time timer, destroys the local widget and
checks `stage-group gate self-test: the finish release ran once with the
composition-time resource still held`, reading `count=1 armed=1
path=<complete|skipped-to-end|timeout|watchdog> ending={...}`, so every path
writes exactly one release PASS before `SCENARIO_RESULT`.
