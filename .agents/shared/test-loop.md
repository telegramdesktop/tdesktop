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
- `EVIDENCE_DIR` — per-run logs and screenshots; defaults to `TASK_DIR` unless the wrapper passes a
  run-specific directory.
- **TASK SPEC** — the task's self-contained `task.md`, including its design
  basis when the wrapper records one, plus any referenced images (`images/<file>` mockups /
  screenshots / graphic resources). Images are optional evidence: read them when present, but their
  absence is never by itself a planning, implementation, or test blocker. The spec and its cited
  repository/baseline sources are one side of test design; the implementation diff is the other.
- Config: `BUILD` (build command), `EXE` (built binary path), `MAX_ATTEMPTS` (default 4). The test
  account lives in `out/Debug/` as the portable-data folders described under "Test account" below;
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

Early-escalation rule: if two consecutive ASSESS rounds produce the **same failure signature**
(same step fails the same way after a fix), stop and return BLOCKED — do not burn the rest of
the attempt budget chasing it.

UNRECOVERABLE conditions: the app reaches a login screen / `AUTH_KEY_DUPLICATED` and re-copying the
test account does not recover it, or a crash has no usable diagnostic after one retry. Missing
`test_TelegramForcePortable` is a global environment hard stop, not a task `Block`. A file-lock build
error (`LNK1104`, `C1041`, access denied, file in use) is likewise a repository hard stop: do not
retry or work around it; ask the user to close the app and debugger.

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
  the first line.
- **Second line:** empty.
- **Third line:** exactly `Task: <TASK_ID>`.
- **Nothing else:** no explanatory body, `Autotask:`, attempt marker, `Co-Authored-By:`, or any
  tool/assistant attribution. The attempt number is runner state, never part of the message.

## Test account (portable data) — hard rules

The debug build runs in portable mode out of `out/Debug/`. Three sibling folders matter:

- `test_TelegramForcePortable` — the golden test account, prepared by the user. Read-only SOURCE,
  never modified by tests. (Its presence is the launch gate; the wrapper aborts if it is missing.)
- `TelegramForcePortable` — the LIVE folder the app actually uses (its presence is what puts the
  build in portable mode). Disposable; recreated fresh each run.
- `real_TelegramForcePortable` — the user's real data, preserved once so manual use survives.

**SETUP — run at the START of every test run, with NO app instance alive. Idempotent: it
guarantees a clean test account no matter how the previous run ended.**
1. Require `test_TelegramForcePortable`. Its absence is the only portable-account setup blocker.
2. If `TelegramForcePortable` exists and `real_TelegramForcePortable` does not, rename the live
   folder to `real_TelegramForcePortable`.
3. If both `TelegramForcePortable` and `real_TelegramForcePortable` exist, recursively delete the
   live folder completely. Do not inspect or require an ownership marker: coexistence means the real
   data is already preserved and the live folder is disposable.
4. Deep-copy `test_TelegramForcePortable` to `TelegramForcePortable`. Never rename, modify, or
   delete the golden folder.

After SETUP, the live folder is always a fresh deep copy of the golden account, the golden folder is
still present, and the real folder may or may not be present. An existing live folder, an existing
real folder, both folders existing, or any missing/extra ownership marker is never a blocker.

**CLEANUP — optional, only after SETUP completed successfully.** The SETUP steps already self-heal,
so cleanup exists only to leave the user's real data live for manual use. If SETUP stopped because
the golden folder was missing, do not touch any of the three folders.
1. Recursively delete `TelegramForcePortable` completely.
2. If `real_TelegramForcePortable` exists, move it to `TelegramForcePortable`.

Why this is safe: SETUP preserves the live folder as `real_...` before the first test run, and once
`real_...` exists every live folder is a disposable test copy or the manual-use copy restored from
that same preserved folder by CLEANUP. A skipped or interrupted CLEANUP therefore self-heals on the
next SETUP. Use the platform's recursive copy/move operations only on these exact resolved sibling
paths; never delete `test_...` or `real_...`.

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
this path-scoped kill.

**Avoid account-fatal calls; cloud data is otherwise fair game.** The overlay must never trigger
logout / session-termination / account-deletion, and must not wipe the account wholesale. Tests that
genuinely need those use a separate burner account, not this one. (If a permanent destructive-call
fuse is later added to the debug build, this is enforced in code; until then it is the
test-author's responsibility.) Everything short of that is allowed: this is a test-server account,
so freely CREATE content in any chats (messages, drafts, tables, media) and freely DELETE or clear
content that test runs created — including leftovers from previous runs and sessions (e.g. clear
the self-chat rich compose cloud draft before a run instead of designing around accumulated junk;
cloud drafts survive the local tdata restore, so server-side cleanup is the right tool). Don't
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
3. **Cover every surface the task names.** If the Observable result lists a settings row, a balance
   header, a gift field, and a suggestion bar, each must be observed (or explicitly marked N/A with
   a reason). Do not stop at one or two.
4. **Write the checks into `<WORK_DIR>/test.md` BEFORE running** (format under "Test report"), so the
   design is explicit and Actual/Result can be filled in per check afterward.

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

The overlay is ad-hoc, authored fresh against the CURRENT implementation, injected at the
highest level that still exercises the change (often a direct data-layer call like
`item->applyEdition(...)` rather than a faked MTP response). It is also a complete runtime driver
when external desktop control is unavailable. Drive the whole task-specific flow inside the Debug
binary by invoking application actions or posting Qt input events on the event loop, waiting for
observable state, logging assertions, capturing the rendered target in-process, and quitting. A
locked macOS session does not reduce required coverage and is never a testing blocker. The overlay
must:

- Live entirely inside `#ifdef _DEBUG` blocks.
- Pick a **test strategy** and record it in the spec:
  `live-data` (use real account data) · `live-mutate` (really create an entity — prefer a
  throwaway target, clean up after) · `inject` (build fake local state without the network) ·
  `mock-api` (intercept specific requests, return canned responses — for payments/destructive).
  Prefer `inject` over `live-mutate` to avoid account/server accumulation and flake.
- Drive the scenario on the Qt event loop, preferring **condition-waits over fixed timers**
  (wait until the target widget/data actually exists, with a timeout fallback). Fixed sleeps are
  the main source of screenshot flake.
- Write a flushed log to `<EVIDENCE_DIR>/test_log.txt` (open Append|Text, flush after each write) and
  save screenshots to `<EVIDENCE_DIR>/screenshots/`. Delete the old log at the first step.
- **Capture the target tightly.** Grab the specific widget / row / glyph (or crop the saved PNG to
  it) so the target is unambiguously in frame at usable resolution. A full-window grab that leaves
  the target clipped, off-screen, or thumbnail-sized is NOT acceptable evidence — if the target
  isn't clearly captured, that is a TEST_FLAW (re-frame), never a pass.
- When the desktop is locked or an OS screenshot is unavailable, capture from inside the process
  with `QWidget::grab()` or a renderer-owned image after layout and paint have completed. A widget or
  test-window grab plus logged geometry is primary visual evidence; never wait for unlock merely to
  obtain a desktop screenshot.
- **Lay down the oracle's references.** Save every applicable independent reference beside the
  crop. Exact asset work saves OLD and intended-NEW art as `<name>_{old,new}.png`. Without target
  artwork, save the baseline/reference-component crop when available and log the contract anchors,
  style/resource identities, and measurements. Never fabricate an `_new` image.
- Emit these markers, one per line:
  `TEST_STEP: <desc>` · `TEST_RESULT: PASS: <what>` / `TEST_RESULT: FAIL: <what> - <details>` ·
  `SCREENSHOT: <full path>` · `TEST_COMPLETE` (immediately before quit).
- Prefer asserting on **logged state** (log the actual value, assert on text — deterministic);
  reserve screenshots for genuinely visual checks where an eye is the right judge.
- **Watchdog:** install a `QTimer` at scenario start that force-quits (`Core::Quit()`, and if
  needed `std::abort` after a flush) at a hard wall-clock cap (default 120s). This guarantees the
  app never hangs holding a lock on the exe — independent of the runner's own timeout.
- End every path (success or assertion failure) by logging `TEST_COMPLETE` then `Core::Quit()`.

### Finding widgets in an overlay (CRITICAL — avoids a guaranteed crash)

Telegram's custom widgets (`Ui::InputField`, `Ui::FlatLabel`, `Ui::RpWidget`, boxes, buttons, …)
do **NOT** declare `Q_OBJECT` — they have no own meta-object. So `QObject::findChildren<T*>()` does
**not** filter by type for them: with no distinct meta-object it matches the nearest moc'd base
(`QWidget`), i.e. it returns **every** child widget blindly cast to `T*`. The moment you use one as
`T` (e.g. call `InputField::setFocused()` / `rawTextEdit()` on what is really a `VerticalLayout`) you
get a raw SIGSEGV — the debugger shows `this` with the *wrong* dynamic type. A clean rebuild does NOT
fix it; it is a real bug in the overlay, not a stale build.

- **Never** `findChildren<Ui::SomeCustomWidget*>()`. Instead enumerate `findChildren<QWidget*>()`
  (`QWidget` *is* `Q_OBJECT`, so that call is sound and returns all descendants) and
  `dynamic_cast<Ui::SomeCustomWidget*>()` each, keeping the non-null results — C++ RTTI identifies the
  real type regardless of `Q_OBJECT`. A reusable helper:
  ```cpp
  template <typename T>
  [[nodiscard]] std::vector<T*> FindWidgets(QWidget *root) {
      auto out = std::vector<T*>();
      for (const auto w : root->findChildren<QWidget*>()) {
          if (const auto t = dynamic_cast<T*>(w)) out.push_back(t);
      }
      return out;
  }
  ```
- Only genuine Qt `Q_OBJECT` types (`QWidget`, `QLabel`, `QLineEdit`, …) are safe to pass directly to
  `findChildren<T*>()`.

### Log to an ABSOLUTE path (the launcher chdir's)

The Windows launcher changes the working directory to the exe folder before the app runs, so a
**relative** overlay log path (`<EVIDENCE_DIR>/test_log.txt`) silently fails to write (`QFile` won't
create missing parents) — the run looks "clean" but produces no evidence. Create and resolve
`EVIDENCE_DIR` to an absolute path up front (or bake its absolute path into the overlay) so flushes
actually land; likewise for screenshots.

### Git mechanics for the overlay (no stash)

- Before authoring, inventory every tracked overlay path in `<WORK_DIR>/test-overlay.paths`; no
  unrelated or untracked source path may be used. After building, save the overlay with
  `git diff --binary HEAD > <WORK_DIR>/test-overlay.patch` and verify the patch is nonempty and
  reapplicable. Restore only the inventoried overlay paths to `GREEN_REF`; never hard-reset the
  repository. The overlay never enters an impl commit.
- Next round, re-apply on top of the new implementation: `git apply --3way
  <WORK_DIR>/test-overlay.patch`. This succeeds ~90% of the time when the tail change was small.
- On conflict, **re-author the conflicting hunk from the latest Attempt/Run in `<WORK_DIR>/test.md`** (which
  records injection point, fake values, and assertions) rather than fighting conflict markers.
  Scenario steps that only call public APIs should live in their own block so they never conflict;
  only true in-situ injections land inside impl files.

## Build & run discipline

- On macOS, a locked graphical session disables external UI driving only. Launch `EXE` normally,
  run the in-binary overlay flow, collect its logs and widget/window grabs, assess them, and clean up.
  Do not try to unlock the session and do not return BLOCKED because the lock screen is present.
- Build with `BUILD`. A single changed TU compiles fast; only the overlay-touched files + link
  rebuild between rounds. Proactive path-scoped cleanup may run before the build. If the build reports
  `LNK1104`, `C1041`, access denied, or file in use, follow `AGENTS.md`: stop immediately, do not
  retry or attempt a workaround, and ask the user to close the app/debugger.
- **Codegen does not track resource mtimes.** If the task changed only a resource the style codegen
  consumes (an icon `.svg`, etc.) without touching a `.style`, an incremental build will NOT re-pack
  it and the binary keeps the OLD asset. Before building such a task force regeneration — touch the
  referencing `.style` (or clean the codegen output) — so the change actually ships. A render that
  shows no difference from before is the symptom of skipping this.
- Run: run the SETUP steps (Test account) -> create `EVIDENCE_DIR` -> launch `EXE` **with
  `-testagent`** in the background, redirecting stdout to `<EVIDENCE_DIR>/app_stdout.txt` and stderr
  to `<EVIDENCE_DIR>/app_stderr.txt` (this flag prevents modal crash hangs, and stderr captures
  assertion text) -> **start a hard wall-clock deadline (~90s) from launch** -> poll
  `<EVIDENCE_DIR>/test_log.txt` every ~5s -> on each `SCREENSHOT:` read the image and judge it -> detect
  `TEST_COMPLETE` (success) or process death (crash) or no new output for the watchdog cap, or the
  hard deadline elapsing (hang) -> path-scoped kill of any straggler (Test account → "Serialize app
  runs") -> optional CLEANUP -> save the binary overlay patch -> restore only inventoried overlay
  paths to `GREEN_REF` (the patch must be saved before this restore).

    On Windows, launch and capture both streams like:

        $exe = (Resolve-Path "$EXE").Path
        Start-Process -FilePath $exe -ArgumentList '-testagent' `
          -RedirectStandardError "$EVIDENCE_DIR/app_stderr.txt" `
          -RedirectStandardOutput "$EVIDENCE_DIR/app_stdout.txt" -PassThru

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
`<workdir>/tdata/working` exists. So **always pass `-testagent`**, and on a crash gather diagnostics
in this order before deciding the verdict:

1. **`<EVIDENCE_DIR>/app_stderr.txt`** — the `[testagent] assert: …` line gives the failed expression and
   `file:line` (e.g. `vector(1931) : … vector subscript out of range`). Usually enough to localize.
2. **`<workdir>/tdata/working`** — the crash report the reporter wrote: the `Assertion:` /
   `CrtAssert:` annotations, the failed `file:line`, and `Caught signal …` / minidump id. Plain text;
   read it directly. `<workdir>` is the launch `-workdir` (in portable test runs,
   `out/Debug/TelegramForcePortable/`).
3. **`<workdir>/tdata/dumps/*.dmp`** — the minidump (full stack, needs symbols to read; note its path
   in `test.md`, don't try to symbolize inline).

A crash is an **IMPL_BUG** (the implementation tripped an assertion / dereferenced out of range), not
a TEST_FLAW, unless the overlay itself is what reached out of bounds — quote the `[testagent]` line
and the `tdata/working` excerpt in `test.md` as evidence, and feed the expression + file:line to the
impl-fix agent as the Root cause / Fix hint. Only a crash with NO usable diagnostic after one retry
is UNRECOVERABLE.

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
implementation itself wedged, in which case it is an IMPL_BUG. Two external-cap kills in a row with
the same signature → BLOCKED (early-escalation rule).

### Leave no test binary behind

The on-disk `EXE` (`out/Debug/Telegram.exe`) always contains the compiled overlay after a test run.
Restoring source does not rewrite the binary. When the loop reaches a TERMINAL verdict (APPROVED,
BLOCKED, UNRECOVERABLE, or attempt cap), after the final path-scoped kill and exact-path source
restore, **delete the built `EXE`** so no overlay-laden test binary is left for
the user to launch by mistake:

    Remove-Item -Force "$EXE"

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
#### Failure signature         (one line, for early-escalation comparison)
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
