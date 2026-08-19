# Complete Telegram Task Pipeline

## Contents

- [Contract and inputs](#contract-and-inputs)
- [Preflight](#preflight)
- [Local artifacts and resumption](#local-artifacts-and-resumption)
- [Delegation](#delegation)
- [Implementation phases](#implementation-phases)
- [Verification tasks](#verification-tasks)
- [Telegram commits](#telegram-commits)
- [Test loop adapter](#test-loop-adapter)
- [Final AI state](#final-ai-state)
- [Failure handling](#failure-handling)

## Contract and inputs

Run exactly one already selected `ai-tdesktop` task in its Telegram checkout.
Do not split it, claim other work, process the inbox, or create a second
stateful runner. Treat the external `task.md`, its referenced inputs, project
context, and repository as sufficient unless the request expressly requires
unavailable exact bytes or content.

Use visual evidence in this order: explicit task facts; supplied inputs;
adjacent current UI/code/styles and the pre-task baseline; repository history
and legacy implementations; then the closest established desktop convention
and the smallest common-sense change. Record assumptions and never invent a
reference. Missing optional art is not a blocker and never weakens a visual
task.

Resolve these values before phase work:

```text
SOURCE_ROOT
AI_SLOT
TASK_ID
TASK_DIR = AI_SLOT/tasks/TASK_ID
WORK_DIR = TASK_DIR/work
LOCAL_DIR = TASK_DIR/.local
TASK_SPEC = TASK_DIR/task.md plus referenced TASK_DIR/input files
PROJECT_FILE = AI_SLOT/projects/<project>/project.md, or none
PREVIOUS_CONTEXT = latest approved project task's work/context.md, or none
BASE_REF = refs/ai-tasks/TASK_ID/base
GREEN_REF = refs/ai-tasks/TASK_ID/green
RUN_REF = refs/ai-tasks/TASK_ID/run
```

Capture a wall-clock start time for the final elapsed-time report.

Resolve host kind, build tree, command, executable, and desktop target as one
consistent platform configuration:

```text
native Windows: cmake --build ./out --config Debug --target Telegram
WSL/Linux:      Telegram/build/docker/centos_env/build_debug.sh
macOS/other:    AGENTS.md and the configured Debug tree

EXE candidates:
out/Debug/Telegram.exe
out/Debug/Telegram
out/Debug/Telegram.app/Contents/MacOS/Telegram

TEST_ACCOUNT = out/Debug/test_TelegramForcePortable
MAX_ATTEMPTS = 4
MAX_TEST_RUNS = 12
COMPUTER_USE_POLICY = auto | overlay-only | required
```

On macOS, pass the outer `.app` containing the resolved executable to a UI
driver. Never mix native Windows commands with a WSL tree. Build Debug only.
Proactive process cleanup may target only the exact resolved executable path;
never terminate Telegram by image name.

## Preflight

Before planning or editing:

1. Read `SOURCE_ROOT/AGENTS.md`, `REVIEW.md`, `AI_SLOT/AGENTS.md`, `TASK_SPEC`,
   every referenced input, and relevant project context.
2. Verify `state.yaml` is `in-progress` and owned by this checkout tag.
3. Run the scripted preflight report and act on its JSON instead of composing
   the equivalent shell checks by hand:

   ```bash
   python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     source-preflight --source-root SOURCE_ROOT --task TASK_ID --exe EXE
   ```

   It reports source/submodule cleanliness, dirty paths outside the owned
   write set, and the golden test account and live marker state for `EXE`.
4. Require the prepared portable test account (`golden_account_present`). Its
   absence is a global hard stop before implementation.
5. Verify a usable Debug executable/build tree, safe path-scoped process
   control, safe portable-folder operations, and the ability to launch and
   render the in-binary test flow. A locked macOS session disables Computer Use
   only; it does not fail this preflight or block testing, even when policy was
   `required`.
6. For a new run require a clean tracked Telegram worktree, clean submodules,
   and no unrelated untracked files, then initialize local recovery state:

   ```bash
   python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     source-begin --source-root SOURCE_ROOT --task TASK_ID
   ```

   On resume, run the same command. It verifies the local refs, rediscovers the
   latest exact `Task:` commit from current first-parent history when needed,
   and records current `HEAD` in `RUN_REF`. Later task commits may remain above
   the retained implementation. Never resolve or record a ref's object name in
   an artifact.
7. For an interrupted run, allow dirty Telegram paths only when every one is
   listed in `work/owned-paths.txt` and completed phase artifacts prove this
   task owns them (`dirty_outside_owned` empty in the preflight report).
   Otherwise hard-stop without cleaning them.

Do not stash. Do not reset, restore, stage, commit, or delete an unexpected
path. Invocation authorizes recovery only for paths proven to belong to this
task and only back to `RUN_REF` or `BASE_REF`, as appropriate.

## Local artifacts and resumption

Use tracked, resumable task artifacts:

```text
work/context.md
work/project.proposed.md       # project tasks only
work/visual.md                 # layout tasks only
work/plan.md
work/review1-correctness.md    # one report per review lens per iteration
work/review1-lifetime.md
work/review1-reuse.md
work/review1-structure.md
work/review1.md                # synthesized review for the iteration
work/test-design.md            # check design drafted during review iteration 1
work/test.md
work/result.md
work/owned-paths.txt
work/progress.md
work/logs/phase-*.prompt.md
work/logs/phase-*.progress.md
work/logs/phase-*.result.md
work/test-overlay.patch
evidence/                      # selected durable proof
```

Use ignored local storage for bulky or machine-specific data:

```text
.local/runs/attempt-<n>/run-<m>/
.local/build-logs/
.local/dumps/
```

Keep complete portable accounts, browser/Computer Use profiles, downloaded
components, raw run directories, full build output, and temporary files under
`.local/` or the checkout's existing ignored build tree. Never commit them.

At startup read `phase` plus the existing progress, plan, review, test, and
result artifacts. Resume at the first incomplete validated boundary. Do not
repeat an approved phase merely because the prior agent session disappeared.
Treat a compact subagent reply as a notification; the artifact and repository
state are proof.

At each stable boundary update `work/progress.md` and record the current phase
locally:

```bash
python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
  checkpoint --source-root SOURCE_ROOT --task TASK_ID --phase PHASE
```

Record progress after context, assessed plan, each completed implementation
phase when useful for recovery, the retained implementation commit, review,
and each material test attempt. Never mark a half-written artifact complete.
The helper changes only task-scoped files in the slot worktree and publishes no
checkpoint commit. Keep this local dirty state until the final `Approve` or
exceptional `Block` commit captures the whole task record.

## Delegation

Use `references/phase-prompts.md` for the exact context-and-plan, assessment,
implementation, build, review, test-design, and native-Windows normalization
prompts, plus the host-specific orchestration rules.

- The performer is the only stateful task owner.
- Probe nested mode with the first real leaf phase. If depth, capacity, or
  policy rejects that spawn before work begins, execute the same prompt
  checklists in the performer. This is a supported mode, not degraded failure.
- In nested mode, use a fresh leaf for context-and-plan, assessment, each
  implementation unit, review lenses, test design, review synthesis,
  review-fix, and test authoring. Every leaf must be told not to delegate and
  never to commit.
- Small-task fast path: the performer may run the context-and-plan checklist
  itself, without a leaf, only when the task spec itself names every file to
  touch and the change is mechanical — roughly two source files or fewer, no
  new APIs, strings, or style tokens, no layout derivation. When in doubt,
  delegate. Assessment always runs as a fresh leaf and has the authority to
  reject the fast-path sizing or the plan's whole approach as over-engineered;
  either rejection forces a Phase 1 leaf rerun, an approach rejection with the
  assessor's simpler direction added to the prompt.
- Use `fork_turns: "none"` with explicit paths. Fork the smallest turn window
  only for genuinely unavailable chat-only visual context.
- Inherit the parent's model and reasoning level. Do not invent tool fields.
- Keep implementation units sequential unless the assessed plan proves
  disjoint write sets and capacity makes parallel edits safe.
- Never duplicate the performer or an implementation unit with uncertain
  writes.

Write the delegated prompt first. Require a final reply containing only
status, artifact paths, touched paths, and blocker. On Claude Code, run each
leaf as a synchronous foreground call and validate its artifacts when the call
returns; run independent leaves of one step as parallel calls in a single
message. On Codex, use the asynchronous wait ladder from the phase prompts:
poll no longer than 60 seconds, treat a timeout as not-failure, use artifact
mtimes and heartbeat counters, message the target after five minutes without
movement, and interrupt and retry that disposable phase once after a second
unchanged window. On either host, never replace a live stateful performer.

## Implementation phases

Run sequentially:

1. **Context, visual design, and plan.** One leaf writes a self-contained
   `work/context.md`, then — for `Visual: layout` tasks — `work/visual.md`,
   then `work/plan.md` with exact files, functions, ordered steps, bounded
   phases, owned write sets, Debug build verification, and status checkboxes.
   For project work it also writes `work/project.proposed.md` as a coherent
   finished-state blueprint; use the Phase 1F prompt when prior task context
   exists, otherwise Phase 1 with the project file. Do not promote the
   proposal yet; blocked work must not become project truth.
   The visual contract derives every dimension from request relationships,
   supplied images, font metrics, style tokens, sibling geometry, or a cited
   desktop analogue, with ordered calculations, tolerances, relationship
   checks, same-scale comparison, and an adversarial rejection pass. For
   `Visual: appearance`, keep the lighter exact color/text/glyph oracle. Skip
   the visual step for non-visual work.
   Small-task fast path: under the strict criteria in the Delegation section,
   the performer may run this phase as a same-session checklist producing the
   same artifacts.
2. **Assess.** Independently verify paths and APIs, completeness, design,
   duplication, edge cases, repository conventions, and phase sizing; weigh
   the approach against the closest repository precedent and its containment
   against the shared modules it touches, and reject over-engineering rather
   than refining it; on layout tasks verify the visual contract's anchors and
   derivation; on a fast-path plan verify the sizing itself. Require
   `Phases: <N>` and `Assessed: yes`, or a recorded `Fast-Path: rejected` /
   `Approach: rejected` outcome that reruns Phase 1 as a fresh leaf — an
   approach rejection with the assessor's simpler direction as added input.
3. **Implement.** Run one leaf per assessed plan phase. Before each edit,
   update `work/owned-paths.txt`. A leaf edits only its owned paths and its
   phase status; it does not commit.
4. **Build.** Run the resolved Debug build in the performer. Fix only build
   errors belonging to the task. If the task changed only a resource consumed
   by codegen, force its documented regeneration so the Debug binary contains
   the new resource. On Windows, run the exact-path proactive cleanup before
   every build. Recover file-lock/access-denied failures through
   `.agents/shared/build-lock-recovery.md`; only an exhausted or unsafe
   recovery is a global hard stop.
5. **Review.** Run the multi-lens review/fix loop from the phase prompts for up
   to three review iterations. Each iteration runs four independent lenses over
   the task diff — correctness, lifetime and ownership, reuse, structure — and
   then one synthesis pass that confirms every finding against the code itself
   and writes the single `review<R>.md` the fix phase implements. A lens
   defaults to not clean and must record the surfaces it checked; an approved
   review carries that merged coverage as the evidence for approval. Rebuild
   after every fix pass. Give the correctness and structure lenses the visual
   contract on layout tasks. Alongside the iteration-1 lenses, spawn the
   Phase 6d test-design leaf; it drafts `work/test-design.md` from the spec,
   plan, and current diff so the test loop does not start from scratch.
6. **Normalize.** On native non-WSL Windows, normalize only task-owned source,
   header, style, localization, and build/config text to CRLF without BOM,
   preserving content and trailing-newline state, then rebuild. On macOS,
   Linux, and WSL preserve LF/no-BOM.
7. **Commit and test.** Create the Telegram implementation commit with the
   scripted helper, then run the test loop below:

   ```bash
   python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     source-commit --source-root SOURCE_ROOT --task TASK_ID \
     --subject "<subject with the required conditional [ai] prefix>" \
     --mark-green
   ```

   It verifies every dirty path against `work/owned-paths.txt` (plus the
   optional `tasks/TASK_ID.md` source note), stages exactly those paths,
   writes and validates the exact three-line message, and with `--mark-green`
   moves the retained-implementation refs — replacing manual staging and the
   separate `source-mark-green` call. An implementation bug creates the next
   committed attempt through the same helper; keep the same `Task:` locator on
   every attempt.

## Verification tasks

A task whose `state.yaml` carries `type: verify` measures behavior that already
shipped. Read the type from the `resolve` output before planning; it selects
this profile for the whole run and nothing else does.

A verification carries no implementation. It must not change a byte under
`Telegram/SourceFiles/` or `Telegram/Resources/` outside the disposable test
overlay, and it produces **no Telegram commit at all**. Its deliverable is the
measurement, and its outcome is either "the behavior held" or "here is the exact
disagreement, and here are the follow-up tasks that fix it". Both are `approved`.

### What it does not run

Skip these phases outright rather than running them against an empty diff:

- **Phase 3, Implement.** There is no product write set. The only code a
  verification writes is the test overlay, authored in the test loop.
- **Phase 4, Build,** as a separate implementation step. The overlay build in
  the test loop is the only build.
- **Phase 5's four review lenses** — correctness, lifetime, reuse, structure —
  and the whole three-iteration review/fix loop. They read a product diff that
  does not exist. Step 6d, the test-check design, is the one part that survives,
  and it moves into Phase 2V below.
- **Phase 6, Normalize.** No task-owned source text changes.
- **Phase 7's `source-commit`.** The helper refuses it for this type. There is no
  implementation attempt, no `GREEN_REF`, and no attempt counter to advance;
  `MAX_ATTEMPTS` does not apply, only `MAX_TEST_RUNS`.

### What it does run

1. **Phase 1V: context and measurement plan.** One leaf writes `work/context.md`
   and then `work/plan.md` as a measurement plan rather than a change plan: the
   exact claim under test stated as a falsifiable proposition, the oracle that
   decides it, the fixture and how it is obtained, the surfaces to be read, and
   the literal values to be quoted before and after. Where the spec names a
   control — a sibling widget that must measure differently, a pre-change render
   reconstructed from history, a negative case that must not fire — the plan
   carries it as a required check, not as an optional extra.
2. **Phase 3V: assess for falsifiability.** A fresh leaf verifies the plan the
   way Phase 2 verifies an implementation plan, against one question the four
   review lenses never ask: *would this run have detected the negative?* It
   rejects a plan whose oracle passes when the behavior is absent, whose control
   cannot disagree with the subject, or that reads local state where the claim is
   about persisted or server state. It also writes `work/test-design.md`, so the
   test author starts from a reviewed design. Require `Assessed: yes` and a named
   falsifier for every check.
3. **Test loop.** Enter it directly after assessment. Everything in the adapter
   below applies unchanged except that it starts here instead of after a green
   implementation commit, and `overlay-apply` is never needed because no
   implementation-fix commit ever moves `RUN_REF`.

`source-begin` still runs in preflight and still sets `BASE_REF` and `RUN_REF`;
they stay equal for the whole task, which is exactly what makes `overlay-save
--restore run` restore the checkout to its untouched baseline. `work/owned-paths.txt`
lists the overlay paths only. For a verification it keeps the preflight's
`dirty_outside_owned` honest; it never authorizes a commit.

### Failures belong to the subject, not the run

A verification that finds the behavior wrong has succeeded. Record the exact
disagreement — expected, actual, literal values, quoted — report it as a
discovered follow-up, and finish `approved`. **Do not repair what you measured**,
even when the fix looks like one line: the repair is ordinary implementation work
in a separate task, and making it here would destroy the measurement's
independence and produce a commit this task may not make.

Reserve `blocked` for a measurement that could not be taken at all — the fixture
is unobtainable, the surface cannot be reached, the oracle needs infrastructure
this checkout does not have. A negative result is never a block.

A verification also never widens its own scope. If the run reveals a second
untested behavior, that is another discovered follow-up, not another check.

## Telegram commits

The performer owns commit boundaries. The workspace helper's `source-commit`
command is the standard mechanism: it enforces this section's contract —
every dirty path verified against the union of owned write sets, only explicit
paths staged, never `git add -A`, the exact three-line message — in one
deterministic call. Commit an intended submodule first, manually, only when
its preflight was clean and all of its changes belong to this task, then stage
the superproject pointer; the helper refuses dirty submodule pointers so an
unintended one can never slip into an attempt.

Every implementation or implementation-fix commit message is exactly:

```text
<conditional [ai] prefix><concise plain-language subject, about 50-60 characters>

Task: <full TASK_ID>
```

Determine the prefix from the retained task implementation, not from the
temporary test overlay. Start the subject with exactly `[ai] ` when the task
changes permanent test-helper code, the agent harness, or agent documentation
in any way. This includes `Telegram/SourceFiles/test/`, `.agents/`, `.claude/`,
`AGENTS.md`, `CLAUDE.md`, and files whose sole role is supporting those
systems. The disposable test overlay and external AI task artifacts do not
count. For every other task, the subject must not contain `[ai]` anywhere. The
prefix counts toward the subject length.

Do not add a body, `Autotask:`, attempt marker, `Co-Authored-By:`, assistant
attribution, or any other trailer. Track rationale in the AI task. If a short
durable explanation will help source-history readers, write
`SOURCE_ROOT/tasks/TASK_ID.md` and include it in the same commit.

Record only the attempt number. Use `BASE_REF` as the original local baseline,
`GREEN_REF` as the latest retained exact task commit, and `RUN_REF` as the
current clean source tip on which this resumed run operates. Later tasks may
make `GREEN_REF` an ancestor of `RUN_REF`. These refs are local recovery
mechanics: never copy their resolved object names into AI artifacts, source
notes, reports, chat, or commit messages.

## Test loop adapter

Read `.agents/shared/test-loop.md` completely and apply it after the first green
implementation commit, or — for a `type: verify` task, which never has one —
immediately after Phase 3V. Read `references/computer-use-testing.md` when choosing
or operating a UI driver. Retain all task-derived oracle, layout measurement,
overlay, watchdog, crash/assertion, hang, account, attempt, report, and evidence
rules, with these external-task safety adaptations:

- The performer, not leaves, stages and commits every attempt (through
  `source-commit`).
- The overlay is authored against the permanent harness in
  `Telegram/SourceFiles/test/` and normally consists of replacing
  `Telegram/SourceFiles/test/test_scenario.cpp` alone on the first run. That is
  a preference, not a boundary: after a setup or reachability failure, overlay
  code may modify any relevant tracked source path, including initialized
  submodules, to place a disposable probe or direct entry point beside the
  changed production code. Inventory every overlay path in
  `work/test-overlay.paths`; never introduce an untracked source file, commit
  an overlay or submodule injection, or re-implement logging, widget-finding,
  capture, watchdog, or quit mechanics the harness already provides.
- Save and restore the overlay with the scripted helper instead of manual git
  mechanics:

  ```bash
  python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
    overlay-save --source-root SOURCE_ROOT --task TASK_ID --restore run
  ```

  It verifies every dirty path against the inventory, refuses untracked
  files, writes a verified top-level patch plus per-submodule patch bundle
  when needed, and restores only inventoried paths to `RUN_REF` or the
  submodule's current baseline — never a repository-wide hard reset.
  After an implementation-fix commit (`source-commit --mark-green` moves both
  `GREEN_REF` and `RUN_REF`), reapply with:

  ```bash
  python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
    overlay-apply --source-root SOURCE_ROOT --task TASK_ID
  ```

  It applies with `--3way` and reports conflicted paths; re-author a
  conflicting hunk from `test.md` rather than leaving conflict markers.
- On locked macOS, force overlay-only testing without waiting or blocking.
  Encode the complete interaction inside the Debug binary using application
  actions or Qt events, log assertions and geometry, capture widgets/windows
  in-process, save the artifacts, and quit. Do not require an OS-level desktop
  screenshot or interactive Computer Use evidence.
- Missing `test_TelegramForcePortable` is a portable-account setup blocker. A
  `testing` marker file inside the live folder marks it as the reusable test
  copy: never copy, move, or delete any of the three folders on this branch.
  For this reused marked-live account only, after the exact-path straggler kill
  and before launch, `test-run` moves a non-empty live `tdata/working` to
  `RUN_DIR/stale-crash/working` and every live `tdata/dumps/*.dmp` to
  `RUN_DIR/stale-crash/dumps/`; a zero-byte `tdata/working` is neither moved
  nor reported. The report is moved because leaving it makes the app show the
  previous-launch window instead of starting, so no `test_log.txt` is written
  and the run reads as a hang. A leftover dump never blocks launch and is
  moved only to keep the current run's mtime-filtered dump report free of old
  minidumps. After successful relocation, the next SETUP finds nothing left
  to move. An unmarked live folder is real data: move it to real when real is
  absent; delete it only when real already exists. Only then deep-copy golden
  to live and create the `testing` marker inside the copy. The four account
  results remain `fresh-copy`, `reused-marked-live`, `preserved-real`, and
  `replaced-manual-live`; clearing creates no fifth account state. There is NO
  folder cleanup after testing — the marked copy stays live for the next run
  and next task. Never delete, rename, move, or alter golden or real.
- Set `RUN_DIR` and `EVIDENCE_DIR` to
  `TASK_DIR/.local/runs/attempt-<n>/run-<m>/`, an ignored run-specific path.
  Promote only decisive compact logs/screenshots and decisive preserved
  `RUN_DIR/stale-crash/` payloads into tracked `evidence/`; keep promotion
  selective rather than committing every large dump.
- Execute every app run through the scripted runner instead of hand-composed
  launch/poll/kill shell:

  ```bash
  python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
    test-run --exe EXE --run-dir RUN_DIR [--env NAME=VALUE ...] \
    [--deadline 120] [--quiet 60]
  ```

  One call performs the idempotent portable-account SETUP, the path-scoped
  straggler kill, stale-crash relocation for a reused marked-live account
  after that kill and before launch, and the `-testagent -noupdate` launch
  (never auto-update a test binary) with stdout/stderr capture and
  `TDESKTOP_TEST_EVIDENCE_DIR` set to `RUN_DIR`. It enforces the external
  wall-clock deadline and quiet-log watchdog and returns one JSON report:
  outcome, `TEST_COMPLETE` state, parsed
  `TEST_STEP`/`TEST_RESULT`/`SCREENSHOT` markers, stderr tail, fresh
  `tdata/working` crash excerpt, minidump paths, and
  `stale_crash_cleared`. That field is an ordered list of
  `{from, kind, to}` entries whose `kind` is `"report"` or `"dump"`, and is
  `[]` when nothing was cleared. If a stale report cannot be moved, `test-run`
  refuses before launch, prints the helper error on stderr, exits non-zero,
  and emits no JSON. If a dump cannot be moved, the run leaves it in place,
  records its entry with `"to": null` (a null destination), and continues to
  launch. The performer then judges the evidence itself — the runner gathers,
  it never assesses. Crash detection keys on process death without
  `TEST_COMPLETE` plus a fresh `tdata/working`, not exit code.
- If the account breaks mid-loop (login screen, `AUTH_KEY_DUPLICATED`), run
  `test-account-reset --exe EXE` — it deletes only a marked live copy and
  re-copies golden — then retry once.
- Enforce the in-app watchdog too. Count test runs independently from
  implementation attempts and stop at `MAX_TEST_RUNS`.
- A repeated setup failure is not a reason to stop below that cap. Apply the
  shared test loop's directness ladder: preserve what the run proved, forbid
  the failed fixture technique, and make the next overlay more manual and
  closer to the changed production seam. Once a setup outside the task's diff
  fails repeatedly, bypass that setup rather than continuing to test it.
- Plan the fewest possible runs: one complete programmed scenario per attempt
  that proves every check in a single execution, splitting only for checks
  that cannot share one process lifetime. `MAX_TEST_RUNS` is a safety cap,
  never a budget to spend on fragmenting one scenario into several.
- **That rule governs how checks are packed, never how many are taken. A
  coverage gap you find mid-task is closed by another run, not by a follow-up
  task.** When you discover a check this task's acceptance needs and this
  checkout can take — a parameter the scenario only sampled (a subset of an
  enum, one interface scale, one context), a surface reachable only behind a
  different launch flag, a persisted or server value that only a fresh start
  re-reads, a wire path an in-process assertion never exercised — take it now.
  Extend the current scenario when the check can share the process; add a run
  when it cannot. Do this even after every planned check has passed, and even
  while writing `work/result.md`.
  Deferring it is the expensive choice, not the cheap one: this process already
  holds the context, the branch, the overlay and the build, and a follow-up task
  rebuilds all four from nothing before it can take the same measurement. One
  more run costs minutes; the task that replaces it costs a full lifecycle and
  lands days later. Runs added for coverage are not implementation attempts and
  never advance `MAX_ATTEMPTS`; they count only against `MAX_TEST_RUNS`.
- Start the test author from `work/test-design.md` when the review-phase
  draft exists; the author still reconciles every drafted check against the
  final retained diff before writing overlay code, and owns `test.md`.
- On every terminal test exit, run `test-cleanup --exe EXE --delete-exe` so no
  straggler survives and no overlay-bearing Debug executable is left for the
  user to launch accidentally.
- **On a `type: verify` task the `IMPL_BUG` branch does not exist.** The shared
  loop classifies a failing check as either a flawed test or an implementation
  bug to fix and recommit; a verification may do neither. Classify a failing
  check as `TEST_FLAW` only when the fault is in the overlay, the oracle, or the
  fixture — that is still a real flaw and still gets fixed and rerun. When the
  overlay is sound and the *subject* behaves differently than the claim, that is
  not a bug to repair here: it is the measurement's result. Stop the loop, record
  it as `Finding: deviation` with expected and actual quoted literally, and route
  the repair as a follow-up. Re-running a sound measurement until it agrees with
  the claim is the one failure mode this task type exists to prevent.
- When a task needs an out-of-scope fence, snapshot it with
  `fence-create --file <baseline> --root SOURCE_ROOT <paths...>` and verify it
  before publication with `fence-check`.

The test author must read the full task specification and every current-branch
commit whose message has this task's exact `Task:` line. For an uninterrupted
contiguous run this is the `BASE_REF..GREEN_REF` diff; for a resumed older task,
combine the exact task commits and inspect their current code at `RUN_REF`
without treating intervening tasks as this task's changes. It writes checks
before running, covers every acceptance surface, declares a falsifiable oracle
for each, compresses all checks into the fewest possible runs — normally
exactly one — and never reuses a generic navigate-and-screenshot scenario. When
a surface cannot be covered in the packed scenario, the author adds a run for it
rather than dropping it: run count is the thing to minimize once coverage is
settled, never the reason to leave a reachable surface unmeasured. Where an
acceptance criterion ranges over a parameter — every value of an enum, both
halves of a branch, more than one interface scale — the author iterates the
range rather than sampling it, because a hand-picked subset is exactly the shape
of gap that comes back later as its own task. Missing or ambiguous evidence is
`TEST_FLAW`; no expected task delta is `IMPL_BUG`. Repeated failure signatures
trigger the shared directness ladder, not an automatic block. Block before
`MAX_TEST_RUNS` only after a fresh recovery assessment proves that every
applicable more-direct strategy is unsafe, unavailable, or would bypass the
changed code. The macOS cached-language signature first gets the shared test
loop's one-time Xcode clean-rebuild recovery. A known implementation bug at the
attempt cap is implementation-blocked, not a successful retained commit.

Skip runtime testing only for a task with no runnable behavior. Record
`NOT_APPLICABLE` and exact file-level validation. Configuration alone is not a
reason to skip.

## Final AI state

Before publishing an approved result or genuine blocked boundary, require a
clean Telegram checkout at `RUN_REF`, with `GREEN_REF` in its history when an
implementation is retained, no overlay in source, and no overlay-bearing
executable. The marked live test copy stays in place per the test-loop folder
rules. For implementation-blocked work with no
retained commit, restore only proven owned paths to `BASE_REF`. For test-blocked
work retain the latest implementation commit and state the exact unverified
behavior.

Write `work/result.md` with exactly one value for every field:

```text
# Task result: <TASK_ID>
STATUS: DONE | BLOCKED
Verdict: APPROVED | NOT_APPLICABLE | <specific blocker>
Blocker-Type: none | test | impl | unrecoverable
Attempts: <n>
Test-Runs: <n>
UI-Driver: overlay | hybrid | mixed | hybrid-unavailable | not-applicable
Touched: <source paths or none>
Test-Report: work/test.md | not-applicable
Evidence: <tracked evidence paths and what they prove>
Unverified: none | <exact behavior and manual follow-up>
Checkout: clean-buildable | unsafe
Discovered: none | present

## Discovered tasks
<complete independently testable follow-ups, or omit>
```

A `type: verify` task writes one more field and is held to three extra rules,
all enforced by `finish`:

```text
Finding: confirmed | deviation | inconclusive
```

`Touched:` is always `none`. `Finding: deviation` requires `Discovered: present`,
because a measured disagreement that routes no repair is how a known defect
becomes invisible. `Finding: inconclusive` is the only finding a blocked
verification may carry, and it may never be approved — which is what keeps
"the measurement failed" and "the behavior failed" from collapsing into each
other. State the finding against quoted literal values in `work/result.md`, never
as a summary judgement.

For approved project work, promote `work/project.proposed.md` to the project's
`project.md` immediately before final AI publication. For blocked work, retain
the proposal only as a task artifact.

Publish final AI state only after the Telegram commit and result are final:

```bash
python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
  finish --source-root SOURCE_ROOT --task TASK_ID \
  --status approved|blocked
```

The helper verifies a clean source checkout, local task refs, current `HEAD`,
and the retained implementation's exact three-line commit message. It commits
all task-scoped local artifacts and final state as `Approve <TASK_ID>` or the
exceptional `Block <TASK_ID>`, fetches newer canonical state when configured,
rebases the slot, publishes without force, and fast-forwards local AI master.
It deletes all local task refs after approval; after a block it deletes only
`RUN_REF` and retains implementation recovery refs for the next invocation.
Do not report final state until that AI commit reaches canonical master.
Preserve an unpublished final slot commit on a semantic conflict or remote
outage and hard-stop instead of pretending completion.

When `Discovered: present`, preserve complete task blocks in `result.md`. The
`continue` scheduler must route them through the same independent-testability
planner into new unclaimed dated tasks before selecting more shared work.

`Unverified:` records what this run **could not** prove, never what it merely did
not get to. Before writing a non-`none` value, ask whether this checkout could
take the measurement now. If it could, the answer is another run and not an
`Unverified:` line: go back to the test loop and take it, however late that is.
A gap written here becomes a whole new task that must rebuild this task's
context, branch, overlay and build before it can measure what this process is
already holding, so writing one you could have closed trades minutes for days.

What legitimately belongs here is a gap this checkout cannot close: one that
needs a second account, funded external value, real server-backed cloud state, a
purpose-built bot, or hardware this machine does not have. Write it to be
routable — the exact behavior that shipped without verification, and precisely
what closing it would require — so the scheduler can record it rather than
queueing work that would be unstartable the moment it entered the queue.

Scope it to this task's own change, with its acceptance criteria as the boundary.
`Unverified:` is for behavior **this diff** shipped without proof — apply the same
revert test the test loop applies to a check: if reverting this task's diff could
not change the outcome, the gap is about pre-existing behavior and does not belong
on this line at all. Untested code you passed on the way, a neighbouring feature,
a parameter range the acceptance never named, a pre-existing bug you noticed: none
of these are this task's unverified behavior. If one is worth anyone's time it is a
discovered follow-up with its own justification, not a coverage debt this task
incurred.

Never widen `Unverified:` to behavior the task never asked for, and in particular
never to a rationale or motivation sentence in the task body that the acceptance
criteria never encoded — if such a claim is worth verifying it belongs in
acceptance, where the test design will see it and the same run will cover it.
Equally, never narrow it to `none` merely because the acceptance criteria passed.

The reason this boundary is strict is that the codebase is far larger than any
queue. Verification that follows attention rather than the diff has no natural
stopping point, and every entry written past the boundary becomes a task that
delays finishing the work actually in hand.

## Failure handling

- A disposable phase may be retried once through the wait ladder. Never fresh
  retry the performer within the same attempt. An interruption leaves local
  task state `in-progress`; a later `continue` invocation resumes it. A later
  invocation reopens a published blocked task locally without a `Resume`
  commit.
- A clean `blocked` attempt leaves the task unfinished. It lets `continue`
  proceed with independent work, but the next invocation retries it once before
  starting new shared work. A dirty/non-buildable checkout or global
  environment problem stops the current invocation.
- A Windows file-lock build error follows the shared bounded exact-checkout
  recovery. Only exhaustion or an unsafe/non-owned holder stops the run and
  asks the human; the task remains `in-progress`.
- A locked macOS session and the resulting unavailable Computer Use driver
  never stop or block the task; continue with the complete in-binary overlay
  flow.
- Missing optional screenshots or mockups never block.
- Never silently pass unverified behavior. Surface every blocked or partially
  verified task with exact `work/test.md`, `work/result.md`, and evidence paths.
- In Goal mode, report blocked state without claiming achievement;
  complete the goal only when every selected task is approved.
