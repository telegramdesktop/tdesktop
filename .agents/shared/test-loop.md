# Test Loop Protocol (harness-neutral)

The portable core of autonomous, tested implementation. `/perform-task` (Claude Code) and
`$perform-task` (Codex) read it. This file defines shared defaults after one task's implementation
is ready to commit; wrappers own setup, splitting, and spawn/wait mechanics. A wrapper may adapt
commit ownership, task baseline/attempt caps, staging/source restoration, account swapping,
`EVIDENCE_DIR`, or an optional UI driver. Its named rule wins only at that
adapter point; every other rule here still applies.

## Vocabulary

- **task-runner** — the per-task agent (one spawn per task). Owns the loop below. Its context
  is disposable: only its compact final summary propagates up to the orchestrator.
- **impl agent / impl-fix agent** — sub-agents the task-runner spawns to write or fix the
  implementation. They never write test code.
- **test-author agent** — sub-agent that writes the ad-hoc test overlay and builds.
- **overlay** — the throwaway `#ifdef _DEBUG` test code for the current task. Never part of an
  implementation commit. Lives as a patch under the task folder between rounds.
- **golden tdata** — a read-only backup of the authed test account. Tests only ever copy FROM
  it; they never write to it.

## Inputs the wrapper passes in

- `TASK_DIR` — external `ai-tdesktop/tasks/<full-task-id>/` directory for this task.
- `WORK_DIR` — tracked resumable artifacts under `TASK_DIR/work/`.
- `TASK_ID` — full dated task identifier and required source-commit locator.
- `BASE_REF` — local pre-task baseline ref derived from `TASK_ID`.
- `GREEN_REF` — local ref for the current retained implementation attempt.
- `EVIDENCE_DIR` — a required run-specific directory the repository ignores; it holds per-run logs,
  screenshots, and preserved stale-crash payloads.
- **TASK SPEC** — the task's self-contained `task.md`, including its design
  basis when the wrapper records one, plus any referenced images (`images/<file>` mockups /
  screenshots / graphic resources). Images are optional evidence: read them when present, but their
  absence is never by itself a planning, implementation, or test blocker. The spec and its cited
  repository/baseline sources are one side of test design; the implementation diff is the other.
- Config: `BUILD` (build command), `EXE` (built binary path), `MAX_ATTEMPTS` (default 4),
  `MAX_TEST_RUNS` (default 12). The test account lives in `out/Debug/` as the portable-data folders
  described under "Test account" below;
  the wrapper has already confirmed the golden one exists (launch gate). All paths are relative to
  the current checkout — no worktrees are created; the run happens in whatever repository slot it
  was launched from.

## State machine (run by the task-runner)

Precondition: the implementation for this task is ready in the current checkout. The performer
stages exact task-owned paths and commits; leaf implementation agents never stage, commit, or stash.
Move `GREEN_REF` to that commit. The runner tracks the attempt number as its own
state (`attempt` starts at 1); the commit message carries no attempt marker.
Never resolve a local task ref into a hash in any artifact or report. Commits
follow "Commit message" below.

```
TEST_AUTHOR -> RUN -> ASSESS (adversarial — see "Assessing"):
  APPROVED       -> restore overlay paths to GREEN_REF; delete the test binary; return DONE up.
  TEST_FLAW      -> fix the overlay only; back to RUN. Does NOT cost an impl attempt.
  IMPL_BUG       -> spawn impl-fix agent (input = test.md, latest attempt's Root cause / Fix hint);
                    performer commits a NEW attempt and moves GREEN_REF; re-apply overlay
                    (--3way, else re-author); RUN. attempt++
  UNRECOVERABLE  -> delete the test binary; return BLOCKED up with the reason. Stop.
  attempt > MAX  -> delete the test binary; return BLOCKED up with test.md + "improve" notes. Stop.

On every TERMINAL exit (APPROVED / BLOCKED / UNRECOVERABLE / cap) "delete the test binary" means the
step in "Leave no test binary behind" below.
```

Repeated-failure rule: a repeated **failure signature is a demand for a more direct test**, not a
terminal result. Never return `BLOCKED` merely because two runs failed at the same setup step.
A `TEST_FLAW` rerun must stop repairing the same fixture technique and reduce the distance between
the test and the production code this task changed.

Before authoring each recovery run, append a short `Recovery plan` to `test.md` that states:

- what the preceding run positively proved;
- the exact setup assumption that failed;
- the previous technique that is now forbidden;
- the next unused directness strategy and why it can reach the changed code even if the failed
  setup never works.

Choose the next applicable strategy from this ladder. The order is by task fit, not ceremony, and
one recovery may advance several levels:

1. Add diagnostics that identify the exact production object, key, row, request, or callback and
   replace guessed predicates with literal state assertions.
2. Replace synthetic UI/model setup with an established production data-layer insertion API or a
   real disposable `live-mutate` fixture in the prepared test account.
3. Bypass setup behavior outside this task's diff through a narrow inventoried `_DEBUG` in-situ
   seam immediately before the changed production function; construct the object by hand or call
   the real production collector/handler directly, while keeping an independent oracle.
4. For network and retry behavior, inject or mock the exact request result / server error at the
   narrowest transport or callback seam that still executes the changed retry code. Do not wait on
   a live server when the response is not itself the subject.
5. When physical interaction is the subject, drive the exact visible target with real Qt events or
   the safe hybrid driver. On locked macOS, make this direct interaction in-binary; the lock screen
   never prevents a more manual overlay.

After the same signature repeats, use a fresh test-recovery leaf and explicitly forbid the failed
approach in its prompt. Early `BLOCKED(test)` is allowed only when a fresh recovery assessment
records why every applicable unused strategy above is unsafe, unavailable, or would bypass the
changed code, and the performer confirms that record. Otherwise continue until approval,
implementation diagnosis, or `MAX_TEST_RUNS`. The macOS cached-language startup signature still
gets the one-time clean-rebuild recovery under "Crashes & assertions" before entering this ladder.

UNRECOVERABLE conditions: the app reaches a login screen / `AUTH_KEY_DUPLICATED` and re-copying the
test account does not recover it, or a crash has no usable diagnostic after one retry and the
macOS cached-language recovery below does not apply. Missing `test_TelegramForcePortable` is a
global environment hard stop, not a task `Block`. An unmovable stale-report refusal is also a
global environment hard stop, not a task `Block`: report the exact helper refusal, consume no
implementation attempt, do not immediately retry it, and wait for the external lock or permission
condition to be resolved. On Windows, recover a file-lock build error (`LNK1104`, `C1041`, access
denied, file in use) through `.agents/shared/build-lock-recovery.md`; only an exhausted or unsafe
recovery is a repository hard stop.

## Handoff tokens

- **Owned paths plus phase artifacts** are the implementation-leaf handoff. The performer inspects
  them, stages only explicit task-owned paths, and commits per "Commit message" below. If an intended
  submodule changed, the performer commits inside it first and then stages the superproject pointer
  in the same logical attempt. Use real commits, never stash. The performer moves local
  `GREEN_REF` to the resulting commit.
- **Test report** (`test.md`) is the only fix-agent handoff. Give it the latest Attempt/Run section,
  especially Root cause / Fix hint and Failure signature. Reserve wrapper-owned `result.md` for the
  final AI result or exceptional blocked boundary; never create `result<n>.md`.

## Commit message

Impl commits must read like the repository's own history and carry only the durable task locator.
Match the style of recent `git log` subjects.
- **Subject:** one concise, plain-language line summarizing the change, ≤ ~50-60 characters. This is
  the first line. Start it with exactly `[ai] ` when the retained task implementation changes
  permanent test-helper code, the agent harness, or agent documentation in any way; for every
  other task, it must not contain `[ai]` anywhere. The prefix counts toward the length.
- **Second line:** empty.
- **Third line:** exactly `Task: <TASK_ID>`.
- **Nothing else:** no explanatory body, `Autotask:`, attempt marker, `Co-Authored-By:`, or any
  tool/assistant attribution. The attempt number is runner state, never part of the message.

The triggering scope includes `Telegram/SourceFiles/test/`, `.agents/`, `.claude/`, `AGENTS.md`,
`CLAUDE.md`, and files whose sole role is supporting those systems. Classify the retained
implementation only: the disposable test overlay and external AI task artifacts do not count.

## Test account (portable data) — hard rules

The debug build runs in portable mode out of `out/Debug/`. Three sibling folders matter:

- `test_TelegramForcePortable` — the golden test account, prepared by the user. Read-only SOURCE,
  never modified by tests. (Its presence is the launch gate; the wrapper aborts if it is missing.)
- `TelegramForcePortable` — the LIVE folder the app actually uses (its presence is what puts the
  build in portable mode). A marker file named `testing` directly inside it marks it as a
  disposable test copy; a live folder WITHOUT the marker is the user's real data.
- `real_TelegramForcePortable` — the user's real data, preserved so manual use survives. Once it
  exists, NO flow step may ever delete, rename, move, overwrite, or write into it.

**SETUP — run at the START of every test run, with NO app instance alive. It is idempotent: the
first SETUP after a crash moves leftover crash files and can refuse before launch; after successful
relocation, the next SETUP finds nothing left to move.**
The workspace helper's `test-run` command performs exactly these steps before every launch, and
`test-account-reset` performs the broken-account recovery below; the manual steps remain the
contract those commands implement.
1. Require `test_TelegramForcePortable`. Its absence is a portable-account setup blocker.
2. If `TelegramForcePortable/testing` exists, the live folder is already the reusable test copy:
   never copy, move, or delete any of the three folders. Clear only what an earlier run left
   inside the live copy — move a non-empty `TelegramForcePortable/tdata/working` into
   `<EVIDENCE_DIR>/stale-crash/` and every `TelegramForcePortable/tdata/dumps/*.dmp` into
   `<EVIDENCE_DIR>/stale-crash/dumps/`, then proceed straight to testing. `<EVIDENCE_DIR>` must be
   a run-specific directory the repository ignores, never a tracked one: a preserved minidump
   is routinely tens of megabytes, and a tracked destination sweeps it into the wrapper's
   publishing commit. Never delete either: the leftover `tdata/working` is what blinds the next
   run (the app shows its "previous launch was not finished properly" window instead of starting,
   so the run writes no `test_log.txt` and reads as a hang), while a leftover `.dmp` never blocks
   a launch and is moved only to keep a later run's `dumps` report free of old minidumps.
   `test-run` names every moved file and its destination in `stale_crash_cleared`, refuses to
   launch when the report itself cannot be moved, and leaves a minidump it cannot move in place,
   reported with a null destination.
3. If `TelegramForcePortable` exists without the marker, it is the user's real data: move it to
   `real_TelegramForcePortable` when that is absent. If `real_...` already exists, the unmarked
   live folder is the user's manual restore of that same preserved data — recursively delete the
   live folder only.
4. Deep-copy `test_TelegramForcePortable` to `TelegramForcePortable`, then create the marker file
   `TelegramForcePortable/testing` (any content, e.g. `echo 1`). Never rename, modify, or delete
   the golden folder.

Any live/real folder combination is never a blocker. After SETUP the live folder is a marked test
copy, the golden folder is untouched, and `real_...` may or may not exist.

**NO CLEANUP — the flow performs no folder operations after testing, ever.** The marked test copy
stays live, and the three folders are never copied, moved, or deleted between testing phases. SETUP
may move stale crash files from inside the marked live copy before a launch; this is not a folder
operation. The flow never restores real data to live: when the user wants manual use they copy
`real_...` to `TelegramForcePortable` themselves (keeping `real_...` in place), and the next SETUP
handles that unmarked live folder by step 3.

Deletion guard — the only folder the flow may ever delete is a live `TelegramForcePortable` that
either carries the `testing` marker or coexists with `real_...` (step 3). If the test account
breaks mid-loop (login screen, `AUTH_KEY_DUPLICATED`), delete the MARKED live folder, re-run SETUP
for a fresh golden copy, and retry once; if it is still broken the run is UNRECOVERABLE. Never
delete or alter `test_...` or `real_...` under any circumstances.

**Serialize app runs.** Never have two `Telegram.exe` instances alive against this account at once —
concurrent reuse of one auth key can trigger a server-side session reset. Before SETUP, launching, or
rebuilding, kill any straggler **of THIS checkout's binary only** — the one whose full executable
path is `EXE` (`out/Debug/Telegram.exe` in this checkout). Match on the full path; do NOT blanket-kill
every `Telegram.exe` on the machine. The user may be running a system-installed client or another
checkout's build against unrelated accounts — those use different auth keys, never conflict with this
account, and MUST be left alive. On Windows, scope the kill by path:

    $exe = (Resolve-Path "$EXE").Path
    Get-CimInstance Win32_Process -Filter "Name = 'Telegram.exe'" |
      Where-Object { $_.ExecutablePath -eq $exe } |
      ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

`taskkill /IM Telegram.exe /F` is forbidden here and anywhere else in this loop — it is image-name-wide
and takes down the user's unrelated clients. Every "kill stragglers" / "taskkill" step below means
this path-scoped kill. The workspace helper's `test-run` and `test-cleanup` commands implement it
on every platform; prefer them over hand-written kill shell.

**Avoid account-fatal calls; cloud data is otherwise fair game.** The overlay must never trigger
logout / session-termination / account-deletion, and must not wipe the account wholesale. Tests that
genuinely need those use a separate burner account, not this one. (If a permanent destructive-call
fuse is later added to the debug build, this is enforced in code; until then it is the
test-author's responsibility.) Everything short of that is allowed: this is a test-server account,
so freely CREATE content in any chats (messages, drafts, tables, media) and freely DELETE or clear
content that test runs created — including leftovers from previous runs and sessions (e.g. clear
the self-chat rich compose cloud draft before a run instead of designing around accumulated junk;
the live test copy is reused across runs and tasks, so local AND cloud state accumulate — reset
whatever state the test depends on at the start of the run). Don't
delete anything the user placed on the account by hand unless the task says so.

## Design the tests from THIS task (the crux)

The single most important rule: **tests are derived from what THIS task changed — not from generic
project navigation, and not reused from a previous task.** Different change → different checks. If
two tasks produce the same screenshots and the same assertions, the second test is a no-op. Before
writing any overlay:

1. **Read both sides of the task.** (a) The TASK SPEC — its full description, `Design-Basis:` or
   equivalent cited sources, and every referenced image when present. (b) The change under test —
   `git diff <BASE_REF>..<GREEN_REF>` (the complete task diff) and
   `<WORK_DIR>/plan.md`. List every concrete thing the
   diff changed and every surface the task (description + "Observable result") says it affects.
   The diff proves what shipped; it is not independent authority for what the design should be.
2. **Turn each into a falsifiable check with an ORACLE** — something that can come out FAIL. A check
   with no way to fail is not a test. Change types can overlap, so apply every pertinent branch: a
   visible wording change still needs the exact string oracle even when marked `Visual: appearance`;
   add screenshot comparison only when its presentation is separately in scope. By change type:
   - **String / text** → assert the EXACT expected text is present at runtime (dump the label/widget
     text to the log and compare) AND the old text is gone. Not "the screen opened".
   - **Visual / asset (icon, image, color, layout)** → declare the independent target oracle before
     judging the render. For an exact asset replacement, verify any expressly required source-file
     identity/equality, then render the intended and old files and compare both with the tight crop.
     Without target artwork, use the exact task criteria,
     `<WORK_DIR>/visual.md`, cited current/legacy analogues, style-token or resource identity, and
     the pre-task baseline. Confirm a baseline delta whenever the task requires one. **If the target
     still matches the old state when a change is expected, that is a FAIL, not a pass.** A
     `Visual: layout` task must also satisfy every numeric design-contract line (sizes, spacings,
     alignment); supplied artwork is optional and never a prerequisite for that contract.
   - **Behavior** → drive the specific action and observe the concrete state/log/screenshot the
     change should produce, and confirm the pre-change behavior no longer happens.
3. **Cover every surface the task names — and only those.** If the Observable result lists a settings
   row, a balance header, a gift field, and a suggestion bar, each must be observed (or explicitly
   marked N/A with a reason). Do not stop at one or two.
   The same sentence sets the upper bound. You are testing **this change**, not the area it landed
   in. Apply the revert test to every candidate check: *if this task's diff were reverted, could this
   check's outcome change?* If not, it is measuring pre-existing behavior and does not belong here,
   however interesting it looks — a code path that cannot reach the changed lines, a neighbouring
   feature the diff never touches, a pre-existing bug you noticed on the way. Note such a thing as a
   discovered follow-up if it is worth anyone's time, and move on.
   Do not expand the parameter space either. Iterate fully over a range the task's acceptance names
   (every value of the enum it calls out, both halves of the branch it describes), but do not invent
   ranges it does not: the four wallpaper kinds, the other themes, the remaining scales, the sibling
   sections. Existing behavior is not this task's to re-establish, and in a codebase this size a
   verification that wanders into it has no natural end.
4. **Write the checks into `<WORK_DIR>/test.md` BEFORE running** (format under "Test report"), so the
   design is explicit and Actual/Result can be filled in per check afterward.
5. **Run economy — plan ONE run.** A test run costs a build, an app launch, and an assessment pass,
   so compress the whole design into a single programmed execution: one scenario that steps through
   every check on the event loop, emitting per-check markers and saving every log value, measurement,
   and tight screenshot needed to judge all of them afterwards. Order steps so earlier ones do not
   destroy later fixtures. Plan a second run only when two checks genuinely cannot share one process
   lifetime (mutually exclusive fixtures or settings, state one check needs fresh that another
   necessarily contaminates) — never for scenario simplicity. Unplanned re-runs stay what the state
   machine allows: a TEST_FLAW re-run, the next attempt after an IMPL_BUG fix, or the coverage run
   below — and a TEST_FLAW re-author fixes every flaw observed in that run in one pass, not one flaw
   per relaunch.
6. **Coverage run — when you find a missing check, take it here.** Run economy governs how checks are
   packed into runs, never how many checks are taken. If at any point before the task is published you
   find a check its acceptance needs and this checkout can take — a parameter the scenario only
   sampled (a subset of an enum, one interface scale, one of two branch halves), a surface reachable
   only behind a different launch flag, a persisted or server value only a fresh start re-reads, a
   wire path an in-process assertion never exercised — add it now. Extend the current scenario when
   the check can share the process, otherwise run again. Do this even after every planned check has
   passed and even while writing the result. This process already holds the context, the branch, the
   overlay and the build; anything that defers the measurement pays to rebuild all four before it can
   take the same reading. Where an acceptance criterion ranges over a parameter, iterate the range
   rather than sampling it — a hand-picked subset is the most common way a check goes missing. A
   coverage run is not an attempt and never advances the attempt counter.

## Visual contract (layout tasks)

When the wrapper marks a task `Visual: layout`, "looks right" is not a vibe — it is a small
computation, and the test MEASURES it. The wrapper's design-spec phase writes the contract to
`<WORK_DIR>/visual.md`; impl builds to it; this loop verifies it. (Tasks marked `Visual: appearance`
use the ordinary visual/asset check above. Unmarked non-visual tasks use their applicable text or
behavior checks; only legacy unclassified visual changes use the visual/asset branch.)

Build the contract from the strongest available design evidence: explicit request relationships;
supplied references when present; current or legacy task-adjacent UI; then the closest established
desktop component/style token while preserving unspecified behavior. A mockup gives relationships,
never desktop pixels. With no mockup, repository anchors and the written requirement provide those
relationships. The strongest anchor is an existing widget: "the count badge IS the dialogs-list
unread badge" pins font + height + padding to `st::dialogsUnread*` and is self-correcting — far
better than "a blue circle ~24px". Cite each source and record every inference; never invent a
reference or arbitrary geometry merely to fill the contract.

Write it as an ORDERED DERIVATION: each step resolves one quantity the next consumes, so impl and
test are both mechanical. Example — a glyph-on-rounded-square icon + title + count, in a bubble:

    Anchor:  T = st::<title>.font->height ;  Badge := the dialogs unread-badge metrics
    1. glyphH = 1.4·T              ±2px   — white glyph box height                    (from T)
    2. square = glyphH ÷ (2/3)    ±2px   — accent rounded-square side ; iconR = square·0.28
    3. margin m (equal on square's top/left/bottom) ; bubbleH = square + 2·m   ±1px
       bubbleR = bubbleH/2 ; iconR : bubbleR must read as in-sync (icon proportionally smaller)
    4. titleY = (bubbleH − T)/2    ±1px   — title vertically centered in the bubble
    5. badge = Badge (font+height+padding) ; vertically centered ; margins top=right=bottom equal ±1px

Then the RELATIONSHIP checks that catch what existence-checks miss — each falsifiable: `square ≤
bubbleH` (no overflow/overlap), the square's three margins equal, the two corner radii in sync, the
badge identical to a real chat-row unread badge. Note each source-to-desktop adjustment and which
token or metric grounds it; describe mobile→desktop conversion only when a mobile reference exists.

How TEST verifies it (numbers over eyes):
- **Measure, don't admire.** Have the overlay LOG the computed geometry — `font->height` and the
  `QRect` of each piece (glyph, square, bubble, title, badge) — and assert each derivation line
  arithmetically within tolerance. Live-widget geometry is the primary oracle; it deterministically
  catches "icon taller than the bubble", "square overflows", "badge oversized / cramped". Where a
  rect can't be logged, measure it from a tight crop by colour (accent square, badge, bubble outline
  are separable).
- **Same-scale comparison.** When a mockup/reference image exists, put its tight crop and the render
  at equal element height. Otherwise compare the before/after crops or the cited desktop analogue at
  equal scale and annotate the contract measurements. Never judge a small target only in a
  full-window screenshot (a 30px bubble in a 600px window rubber-stamps bad proportions).
- **Adversarial designer pass.** One final judgement framed to REJECT: "You are a product designer
  rejecting this PR — list every way the render violates the cited contract, reference, desktop
  analogue, or preserved invariant." Approve only if it finds nothing disqualifying.
- **Existence ≠ sufficiency.** "Icon + title + count are all present" is a precondition, not a pass.
  A `Visual: layout` check APPROVES only when the measured geometry satisfies the contract; any line
  out of tolerance is an IMPL_BUG (report measured-vs-target) and loops like any other.

## Overlay mechanics

The repository carries a permanent test harness under
`Telegram/SourceFiles/test/` — always compiled, runtime-gated on `-testagent`
(`Test::Active()`), with all of its `#ifdef`s inside the harness itself:

- `test_runner.h` — the staged scenario engine: `Stage{name, run, until, then, timeout}`,
  `waitEvent`, `waitForSessionReady`, the normal bounded non-fatal `waitForChatsLoaded()`, and
  explicit strict `waitForChatsLoadedStrict()`; timing out an ordinary `Stage` ends the whole
  scenario, while the wall-clock watchdog (default 120s, `TDESKTOP_TEST_WATCHDOG` override)
  guarantees `TEST_COMPLETE` + quit on every exit path including timeout.
- `test_log.h` — evidence dir from `TDESKTOP_TEST_EVIDENCE_DIR` (the workspace `test-run`
  helper sets it), flushed absolute-path logging, `Step/Pass/Fail/Check/Note`, `CheckNear`
  tolerance assertions, `LogGeometry`, the standard markers.
- `test_widgets.h` — `FindAll<T>`/`FindFirst<T>`/`FindVisible<T>` (the `dynamic_cast`-based
  finders that avoid the guaranteed `findChildren<CustomWidget*>` crash), `Click`, `TypeText`,
  `PressKey` via real Qt events — each delivered event runs with postponed-call processing
  deferred and is followed by a drain of every pending `Ui::PostponeCall` to empty, so
  postponed input fix-ups AND their own change handling are settled when the helper returns;
  wrap programmatic `setText` in `Test::Settle`.
- `test_capture.h` — `CaptureWidget`/`CaptureRect` (visibility check, `QWidget::grab()` so
  floating elements and locked desktops cannot occlude, automatic blank-image FAIL, geometry
  log, `SCREENSHOT` marker), `Crop`/`Zoom`/`ContactSheet` for tight same-scale evidence.
- `test_agent.h` — `Test::Fire(name)` / `HasFired(name)` named waitpoints;
  `launch_finished` fires at the end of `Application::run()`. `TDESKTOP_TEST_SCALE` is applied
  by the harness at startup.
- `test_scenario.cpp` — the overlay-owned slot: it defines `Test::SetupScenario(runner)` and
  is a no-op in the repository.

A minimal scenario shape:

```cpp
void SetupScenario(not_null<Runner*> runner) {
	runner->waitEvent(u"launch_finished"_q);
	runner->waitForChatsLoaded();
	runner->add({
		.name = u"open the target and verify the row"_q,
		.run = [] { /* trigger the flow under test */ },
		.until = [] {
			return Test::FindFirst<Ui::SomeWidget>(
				Core::App().activeWindow()->widget()) != nullptr;
		},
		.then = [] {
			const auto row = Test::FindFirst<Ui::SomeWidget>(
				Core::App().activeWindow()->widget());
			Test::LogGeometry(u"row"_q, row->geometry());
			Test::CheckNear(row->height(), st::someRowHeight, 1, u"row height"_q);
			Test::CaptureWidget(row, u"target_row"_q);
		},
	});
}
```

**The overlay starts by replacing `test_scenario.cpp` with the task's scenario.** Author the
scenario fresh against the CURRENT implementation from the task's check design; never re-implement
logging, finding, capturing, watchdogs, or quit handling that the harness already provides —
re-derived scaffolding is where capture flaws come from. The scenario is the complete runtime
driver of first resort: prefer
programmatically triggering every required action and judging the saved logs and captures
afterwards over any external desktop driver, whether or not one is available. Drive the whole
task-specific flow inside the Debug binary on the event loop, waiting for observable state,
logging assertions, capturing the rendered target in-process, and quitting. A locked macOS
session does not reduce required coverage and is never a testing blocker. The scenario runs
only when `-testagent` was passed AND the live portable folder carries the `testing` marker,
so it can never run against real account data. The overlay must:

- Prefer keeping the first run centralized in `test_scenario.cpp`, but never treat that module as
  a sandbox boundary. After a setup or reachability failure, inject probes, fixtures, callbacks,
  waitpoints, or direct test entry points at any relevant tracked production location, including
  initialized submodules. Choose the location closest to the changed code that preserves a real
  execution of that code. Scattered test injections are acceptable when they remove fixture
  assumptions; all must remain disposable, inventoried, and excluded from implementation commits.

- Keep any code added outside `test/` inside `#ifdef _DEBUG` blocks only when it would
  change release behavior; harness calls like `Test::Fire` are runtime no-ops and need no
  guard.
- Pick a **test strategy** and record it in the spec:
  `live-data` (use real account data) · `live-mutate` (really create an entity — prefer a
  throwaway target, clean up after) · `inject` (build fake local state without the network) ·
  `mock-api` (intercept specific requests, return canned responses — for payments/destructive).
  Prefer `inject` over `live-mutate` to avoid account/server accumulation and flake.
- Express the flow as `Runner` stages with **condition-waits over fixed timers** (an `until`
  predicate on the target widget/data actually existing, with the stage timeout as fallback).
  Fixed sleeps are the main source of screenshot flake.
- Log through `test_log.h` (`Step`/`Pass`/`Fail`/`Check`/`Note`/`CheckNear`/`LogGeometry`) —
  it already writes the flushed absolute-path log and the exact `TEST_STEP` / `TEST_RESULT` /
  `SCREENSHOT` / `TEST_COMPLETE` markers the external runner parses. Never hand-roll marker
  strings or log files.
- **Capture the target tightly** with `CaptureWidget`/`CaptureRect` — the specific widget /
  row / glyph, unambiguously in frame at usable resolution. A full-window grab that leaves the
  target clipped, off-screen, or thumbnail-sized is NOT acceptable evidence — if the target
  isn't clearly captured, that is a TEST_FLAW (re-frame), never a pass. The helpers grab
  in-process after layout and paint, so a locked desktop never blocks capture and a blank
  grab fails loudly instead of passing silently.
- **Lay down the oracle's references.** Save every applicable independent reference beside the
  crop (`SaveImage`, `ContactSheet` for same-scale comparison). Exact asset work saves OLD and
  intended-NEW art as `<name>_{old,new}.png`. Without target artwork, save the
  baseline/reference-component crop when available and log the contract anchors,
  style/resource identities, and measurements. Never fabricate an `_new` image.
- Prefer asserting on **logged state** (log the actual value, assert on text — deterministic);
  reserve screenshots for genuinely visual checks where an eye is the right judge.
- Rely on the Runner's built-in watchdog and termination: it force-quits at the wall-clock cap
  and ends every path — success, assertion failure, or stage timeout — with `TEST_COMPLETE`
  then quit, so the app never hangs holding a lock on the exe. Do not install a second
  watchdog and do not call `Core::Quit()` from scenario code.

### Finding widgets in an overlay (CRITICAL — avoids a guaranteed crash)

Telegram's custom widgets (`Ui::InputField`, `Ui::FlatLabel`, `Ui::RpWidget`, boxes, buttons, …)
do **NOT** declare `Q_OBJECT` — they have no own meta-object. So `QObject::findChildren<T*>()` does
**not** filter by type for them: with no distinct meta-object it matches the nearest moc'd base
(`QWidget`), i.e. it returns **every** child widget blindly cast to `T*`. The moment you use one as
`T` (e.g. call `InputField::setFocused()` / `rawTextEdit()` on what is really a `VerticalLayout`) you
get a raw SIGSEGV — the debugger shows `this` with the *wrong* dynamic type. A clean rebuild does NOT
fix it; it is a real bug in the overlay, not a stale build.

- **Never** `findChildren<Ui::SomeCustomWidget*>()`. Use the harness finders
  `Test::FindAll<T>` / `Test::FindFirst<T>` / `Test::FindVisible<T>` from `test_widgets.h`,
  which enumerate `findChildren<QWidget*>()` (`QWidget` *is* `Q_OBJECT`, so that call is sound)
  and `dynamic_cast` each result — C++ RTTI identifies the real type regardless of `Q_OBJECT`.
- Only genuine Qt `Q_OBJECT` types (`QWidget`, `QLabel`, `QLineEdit`, …) are safe to pass directly to
  `findChildren<T*>()`.

### Log to an ABSOLUTE path (the launcher chdir's)

The Windows launcher changes the working directory to the exe folder before the app runs, so a
**relative** log path silently fails to write (`QFile` won't create missing parents) — the run
looks "clean" but produces no evidence. `Test::EvidenceDir()` resolves
`TDESKTOP_TEST_EVIDENCE_DIR` (which the workspace `test-run` helper always exports as an
absolute path) and creates it up front, so harness logging and captures are immune; never
bypass it with hand-built relative paths.

### Git mechanics for the overlay (no stash)

- The inventory in `<WORK_DIR>/test-overlay.paths` normally starts with
  `Telegram/SourceFiles/test/test_scenario.cpp` and then lists every in-situ injection or
  `Test::Fire` path. It may name any tracked file in the source checkout or an initialized
  submodule; no unrelated or untracked path may be used. After building, save the overlay with the
  workspace helper's `overlay-save` command: it verifies every dirty path against the inventory,
  writes the top-level patch plus a per-submodule patch bundle when needed, and restores only the
  inventoried overlay paths to their repository baselines; never hard-reset the repository. The
  overlay never enters an impl or submodule commit.
- Next round, re-apply on top of the new implementation with `overlay-apply` (a `--3way`
  application that reports conflicted paths). This succeeds ~90% of the time when the tail change
  was small.
- On conflict, **re-author the conflicting hunk from the latest Attempt/Run in `<WORK_DIR>/test.md`** (which
  records injection point, fake values, and assertions) rather than fighting conflict markers.
  Scenario steps that only call public APIs should live in their own block so they never conflict;
  only true in-situ injections land inside impl files.

## Build & run discipline

- On macOS, a locked graphical session disables external UI driving only. Launch `EXE` normally,
  run the in-binary overlay flow, collect its logs and widget/window grabs, assess them, and clean up.
  Do not try to unlock the session and do not return BLOCKED because the lock screen is present.
- Build with `BUILD`. A single changed TU compiles fast; only the overlay-touched files + link
  rebuild between rounds. On Windows, run the shared exact-path proactive cleanup before every
  build. If the build reports `LNK1104`, `C1041`, access denied, or file in use, follow
  `.agents/shared/build-lock-recovery.md` and retry within its bounded budget.
- **Codegen does not track resource mtimes.** If the task changed only a resource the style codegen
  consumes (an icon `.svg`, etc.) without touching a `.style`, an incremental build will NOT re-pack
  it and the binary keeps the OLD asset. Before building such a task force regeneration — touch the
  referencing `.style` (or clean the codegen output) — so the change actually ships. A render that
  shows no difference from before is the symptom of skipping this.
- Run: execute the workspace helper's `test-run` command with `EXE` and `EVIDENCE_DIR`. One call
  performs the SETUP steps (Test account), creates `EVIDENCE_DIR`, path-scope-kills stragglers,
  then, for a reused marked-live account, moves a non-empty live `tdata/working` to
  `<EVIDENCE_DIR>/stale-crash/working` and every live `tdata/dumps/*.dmp` to
  `<EVIDENCE_DIR>/stale-crash/dumps/` before launch. A zero-byte `tdata/working` is neither moved
  nor reported. It launches `EXE` **with `-testagent -noupdate`** (so a shipped update can never
  replace the binary under test mid-run) capturing stdout to
  `<EVIDENCE_DIR>/app_stdout.txt` and stderr to `<EVIDENCE_DIR>/app_stderr.txt` (the flag prevents
  modal crash hangs, and stderr captures assertion text), enforces **a hard wall-clock deadline
  from launch** and a quiet-log watchdog while polling `<EVIDENCE_DIR>/test_log.txt`, detects
  `TEST_COMPLETE` (success) versus process death (crash) versus the caps elapsing (hang), kills any
  straggler, and returns one JSON report with the parsed markers, stderr tail, fresh crash
  diagnostics, and `stale_crash_cleared`. That field is an ordered list of `{from, kind, to}`
  entries whose `kind` is `"report"` or `"dump"`, and is `[]` when nothing was cleared. If the
  stale report cannot be moved, `test-run` refuses before launch, prints the helper error on stderr,
  exits non-zero, and emits no JSON. If a dump cannot be moved, `test-run` leaves it in place,
  records `"to": null` (a null destination), and continues to launch. Then read each `SCREENSHOT:`
  image and judge it, save the binary overlay patch, and restore only inventoried overlay paths
  (`overlay-save` — the patch must be saved before that restore). The runner only gathers evidence;
  ASSESS below stays the agent's own adversarial judgement.

### Crashes & assertions (always launch the test binary with `-testagent`)

A Debug build normally turns a failed `std::vector` bounds check, a bad iterator, an `assert()`, a
pure-virtual call, or `abort()` into a modal **Abort / Retry / Ignore** dialog. That dialog blocks
the process forever — the agent sees no `TEST_COMPLETE`, no process death, just a hang until the
watchdog cap, and learns nothing about the cause. **`-testagent` removes those dialogs.** With it
set, the binary:

- suppresses every CRT / STL / WER / `abort()` message box (no button to press, never hangs);
- converts any such assertion into a real crash that the crash reporter records, so the process
  **terminates immediately** instead of waiting;
- writes the assertion text (expression + file:line) to **stderr** — captured in
  `<EVIDENCE_DIR>/app_stderr.txt`, tagged `[testagent]`;
- also turns on debug logging (`-testagent` implies `-debug`).

**Do NOT key the crash decision on exit code.** Breakpad handles the crash and the process usually
exits **0** — exactly as tdesktop's own crash detection assumes. The reliable crash signals are: the
process is gone WITHOUT a `TEST_COMPLETE` marker, AND a fresh non-empty
`<workdir>/tdata/working` exists. On macOS, a fresh matching system `.ips`
report is also sufficient when Telegram's reporter wrote nothing. So **always
pass `-testagent`**, and on a crash gather diagnostics in this order before
deciding the verdict:

1. **`<EVIDENCE_DIR>/app_stderr.txt`** — the `[testagent] assert: …` line gives the failed expression and
   `file:line` (e.g. `vector(1931) : … vector subscript out of range`). Usually enough to localize.
2. **`<workdir>/tdata/working`** — the crash report the reporter wrote: the `Assertion:` /
   `CrtAssert:` annotations, the failed `file:line`, and `Caught signal …` / minidump id. Plain text;
   read it directly. `<workdir>` is the launch `-workdir` (in portable test runs,
   `out/Debug/TelegramForcePortable/`).
3. **`<workdir>/tdata/dumps/*.dmp`** — the minidump (full stack, needs symbols to read; note its path
   in `test.md`, don't try to symbolize inline).
4. **macOS `~/Library/Logs/DiagnosticReports/Telegram-*.ips`** — when the preceding files are empty,
   inspect reports created after the exact process launch and match the app UUID/start time. These
   reports can contain a fully symbolicated stack even when Telegram's reporter wrote nothing.

A crash is an **IMPL_BUG** (the implementation tripped an assertion / dereferenced out of range), not
a TEST_FLAW, unless the overlay itself is what reached out of bounds — quote the `[testagent]` line
and the `tdata/working` excerpt in `test.md` as evidence, and feed the expression + file:line to the
impl-fix agent as the Root cause / Fix hint. Only a crash with NO usable diagnostic after one retry
is UNRECOVERABLE.

On macOS, treat this exact repeated startup signature as a stale Xcode incremental build, not an
implementation or overlay verdict:

- the app log stops immediately after `Lang Info: Loaded cached, keys: ...`;
- the `.ips` report shows `SIGABRT` in
  `std::vector<unsigned char>::operator[]` →
  `Lang::Instance::applyValue()` → `fillFromSerialized()` →
  `Local::readLangPack()`;
- the same signature occurs on two launches.

Before changing recovery strategies, preserve the current overlay and account,
stop only the exact-path app,
follow the portable-folder safety-copy procedure in `AGENTS.md`, then run one full Xcode Debug
clean followed by `BUILD` (the configured-tree clean is normally
`cmake --build out --config Debug --target clean`). Restore only portable folders missing after
the clean, never overwrite survivors, and retain the external backup through one successful
post-build launch. Rerun the same scenario once. Record the preceding runs as `TEST_FLAW` caused
by stale generated-language objects; do not spend an implementation attempt or re-author the
overlay. If the identical signature remains after the single clean rebuild, resume normal crash
classification and the directness ladder. Never loop clean rebuilds.

### Hangs & freezes (two layers, because they have two causes)

A run that never reaches `TEST_COMPLETE` and never dies is a hang. Two independent guards catch it:

- **Frozen main thread (in-app).** `-testagent` force-enables the built-in **DeadlockDetector** — a
  ping thread that, if the main/event loop stops responding (a genuine deadlock or an infinite loop
  on the UI thread), raises `Unexpected("Deadlock found!")` from a side thread. That crashes through
  the same reporter, so the **frozen main-thread stack is captured in the minidump** and the process
  exits on its own (key on the `tdata/working` report, not the exit code) — same diagnostics path as
  a crash above. No agent action needed beyond reading `tdata/working` / the dump. Detection is
  within ~30–90s of the stall.
- **Everything else (external hard cap).** The DeadlockDetector does NOT fire when the event loop is
  still alive but the test simply never finishes — e.g. a buggy overlay that loops forever, waits on
  a condition that never comes, or just never calls `Core::Quit()`. For that the **runner enforces a
  hard wall-clock deadline (~90s) from launch** and, when it elapses, does the path-scoped kill
  regardless of output. No legitimate auto-test runs anywhere near a minute, so this cap is pure
  backstop — but it is what guarantees the agent can never wedge forever.

Classify by which guard tripped: a DeadlockDetector crash with a real main-thread stack in app code
is an **IMPL_BUG**; the external cap firing is almost always a **TEST_FLAW** (the overlay didn't
drive to `TEST_COMPLETE`/quit) — re-author the overlay — unless the captured stack/log shows the
implementation itself wedged, in which case it is an IMPL_BUG. Repeated external-cap kills enter
the directness ladder above; the same timeout signature alone never blocks the task.

### Leave no test binary behind

The on-disk `EXE` (`out/Debug/Telegram.exe`) always contains the compiled overlay after a test run.
Restoring source does not rewrite the binary. When the loop reaches a TERMINAL verdict (APPROVED,
BLOCKED, UNRECOVERABLE, or attempt cap), after the final path-scoped kill and exact-path source
restore, **delete the built `EXE`** so no overlay-laden test binary is left for
the user to launch by mistake — the workspace helper's `test-cleanup --exe EXE --delete-exe` does
the final path-scoped kill and the deletion in one call.

A clean, feature-ready binary is one `BUILD` away on demand. (Delete only on terminal exit — between
attempts the next round rebuilds the overlay, so the binary is reused there.)

## Assessing (adversarial)

ASSESS decides APPROVED / TEST_FLAW / IMPL_BUG. Default to **not approved**; a check passes only on
positive, specific evidence — in the captured pixels or the log — that the change is present AND
correct.

- **No pass by inference.** "Same asset so it's fine", "probably", "looks like" are not evidence.
  Missing, clipped, or ambiguous evidence for a check → **TEST_FLAW**: re-frame/re-capture and run
  again. Never turn missing evidence into a PASS.
- **Judge the actual artifact.** State what is literally visible in the crop / present in the log,
  then compare it with the declared oracle sources. Do not narrate expectations.
- **Judge rendered appearance visually, never by hash.** Do not pixel-diff or hash screenshots;
  desktop renders vary by platform, DPI, theme and antialiasing. References convey intent and are
  not pixel targets unless the task expressly says otherwise. A literal exact-file/resource
  requirement may separately assert source bytes, hash, or decoded raster equality, but must still
  verify that the asset renders. Otherwise judge the crop against supplied art or the task's exact
  criteria, cited analogue, token/resource identity, preserved invariants, and numeric contract.
- **No-difference = IMPL_BUG.** If a check detects no difference from the pre-change state (the glyph
  matches the OLD art; the string still shows the old word), the change did not take effect — return
  IMPL_BUG; do not approve.
- **A visual check with no independent target oracle cannot APPROVE.** A supplied image is only one
  possible oracle; exact task facts, `visual.md` geometry, named style/resource identities, or a
  cited current/legacy analogue also qualify. Missing mockups alone never means the oracle is missing.
- APPROVED requires every derived check to PASS with evidence; else IMPL_BUG (real defect) or
  TEST_FLAW (the test was wrong, not the code).

## Test report (`<WORK_DIR>/test.md`) — human-readable, append per attempt

The file the human opens to see how testing went. The test-author writes checks before running;
ASSESS fills Actual / Result and the verdict. Create one `## Attempt` per implementation commit and
append one `### Run` per execution. A TEST_FLAW adds a Run under the same Attempt; an IMPL_BUG fix
starts the next Attempt. Never overwrite history.

```
# Test report — <project>/<letter>: <title>

## Attempt <n>

### Run <m> — strategy <...> — driver <overlay|hybrid> — verdict <APPROVED|TEST_FLAW|IMPL_BUG|UNRECOVERABLE>
- Evidence directory: <EVIDENCE_DIR>

#### Test 1 — <aspect of THIS change>
- Expected: <observable effect the change should produce>
- Oracle: <what would make this check FAIL>
- Oracle source: <task fact / visual.md line / repo analogue / supplied image / baseline>
- Observed via: <surface + how captured: tight crop, geometry log, runtime state>
- Actual: <what is literally visible / logged>
- Screenshots: <after.png and any real reference crops; none only for a non-visual check>
- Result: PASS | FAIL

#### Test 2 — ...

#### Verdict reasoning
<1-3 lines tying the checks to the verdict>
#### Root cause / Fix hint    (only if IMPL_BUG — the impl-fix agent reads this)
#### Failure signature         (one line, for recovery comparison)
#### Recovery plan             (TEST_FLAW reruns only: prior proof, failed assumption,
                                 forbidden technique, next directness strategy)
```

## Compact summary the task-runner returns up

```
TASK: <TASK_ID>
STATUS: <DONE|BLOCKED>
VERDICT: <APPROVED|NOT_APPLICABLE|reason if blocked>
ATTEMPTS: <n>
TOUCHED: <repo paths or none>
DISCOVERED: <none|present in result.md|inline concise follow-ups when the wrapper has no result.md>
NOTES: <one or two lines, or none>
```

Detailed reasoning stays in external AI task artifacts. The chat reply is only this block.
