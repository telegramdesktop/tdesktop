# Complete Telegram Task Pipeline

## Contents

- [Contract and inputs](#contract-and-inputs)
- [Preflight](#preflight)
- [Local artifacts and resumption](#local-artifacts-and-resumption)
- [Delegation](#delegation)
- [Implementation phases](#implementation-phases)
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
3. Require the prepared portable test account. Its absence is a global hard
   stop before implementation.
4. Verify a usable Debug executable/build tree, safe path-scoped process
   control, safe portable-folder operations, and the ability to launch and
   render the in-binary test flow. A locked macOS session disables Computer Use
   only; it does not fail this preflight or block testing, even when policy was
   `required`.
5. For a new run require a clean tracked Telegram worktree, clean submodules,
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
6. For an interrupted run, allow dirty Telegram paths only when every one is
   listed in `work/owned-paths.txt` and completed phase artifacts prove this
   task owns them. Otherwise hard-stop without cleaning them.

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

Use `references/phase-prompts.md` for the exact context, plan, assessment,
implementation, build, review, and native-Windows normalization prompts.

- The performer is the only stateful task owner.
- Probe nested mode with the first real leaf phase. If depth, capacity, or
  policy rejects that spawn before work begins, execute the same prompt
  checklists in the performer. This is a supported mode, not degraded failure.
- In nested mode, use a fresh leaf for context, planning, assessment, each
  implementation unit, review, review-fix, and test authoring. Every leaf must
  be told not to delegate and never to commit.
- Use `fork_turns: "none"` with explicit paths. Fork the smallest turn window
  only for genuinely unavailable chat-only visual context.
- Inherit the parent's model and reasoning level. Do not invent tool fields.
- Keep implementation units sequential unless the assessed plan proves
  disjoint write sets and capacity makes parallel edits safe.
- Never duplicate the performer or an implementation unit with uncertain
  writes.

Write the delegated prompt first. Require an early small heartbeat and a final
reply containing only status, artifact paths, touched paths, and blocker.
Poll no longer than 60 seconds. A timeout is not failure. Use artifact mtimes
and heartbeat counters; after five minutes without movement, message the same
target, and after a second unchanged five-minute window interrupt and retry
that disposable phase once. Never replace a live stateful performer.

## Implementation phases

Run sequentially:

1. **Context.** Write a self-contained `work/context.md`. For project work,
   read the current project file and nearest approved task context, then write
   `work/project.proposed.md` as a coherent finished-state blueprint. Use the
   Phase 1F prompt when prior task context exists; otherwise use Phase 1 with
   the project file. Do not promote the proposal yet; blocked work must not
   become project truth.
2. **Visual design.** For `Visual: layout`, write `work/visual.md`. Derive every
   dimension from request relationships, supplied images, font metrics, style
   tokens, sibling geometry, or a cited desktop analogue. Use ordered
   calculations, tolerances, relationship checks, same-scale comparison, and
   an adversarial rejection pass. For `Visual: appearance`, keep the lighter
   exact color/text/glyph oracle. Skip for non-visual work.
3. **Plan.** Write `work/plan.md` with exact files, functions, ordered steps,
   bounded phases, owned write sets, Debug build verification, and status
   checkboxes.
4. **Assess.** Independently verify paths and APIs, completeness, design,
   duplication, edge cases, repository conventions, and phase sizing. Require
   `Phases: <N>` and `Assessed: yes`.
5. **Implement.** Run one leaf per assessed plan phase. Before each edit,
   update `work/owned-paths.txt`. A leaf edits only its owned paths and its
   phase status; it does not commit.
6. **Build.** Run the resolved Debug build in the performer. Fix only build
   errors belonging to the task. If the task changed only a resource consumed
   by codegen, force its documented regeneration so the Debug binary contains
   the new resource. A file-lock/access-denied build error is an immediate
   global hard stop with no retry or workaround.
7. **Review.** Run the multi-lens review/fix loop from the phase prompts for up
   to three review iterations. Each iteration runs four independent lenses over
   the task diff — correctness, lifetime and ownership, reuse, structure — and
   then one synthesis pass that confirms every finding against the code itself
   and writes the single `review<R>.md` the fix phase implements. A lens
   defaults to not clean and must record the surfaces it checked; an approved
   review carries that merged coverage as the evidence for approval. Rebuild
   after every fix pass. Give the correctness and structure lenses the visual
   contract on layout tasks.
8. **Normalize.** On native non-WSL Windows, normalize only task-owned source,
   header, style, localization, and build/config text to CRLF without BOM,
   preserving content and trailing-newline state, then rebuild. On macOS,
   Linux, and WSL preserve LF/no-BOM.
9. **Commit and test.** Create the Telegram implementation commit, then run the
   test loop below. An implementation bug creates the next committed attempt;
   keep the same `Task:` locator on every attempt. After each clean buildable
   attempt, move the local retained-implementation ref with:

   ```bash
   python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     source-mark-green --source-root SOURCE_ROOT --task TASK_ID
   ```

## Telegram commits

The performer owns commit boundaries. Inspect every dirty path, verify it is in
the union of owned write sets, and stage only explicit paths. Never use
`git add -A`. Commit an intended submodule first only when its preflight was
clean and all of its changes belong to this task, then stage the superproject
pointer.

Every implementation or implementation-fix commit message is exactly:

```text
<one concise plain-language subject, about 50-60 characters>

Task: <full TASK_ID>
```

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
implementation commit. Read `references/computer-use-testing.md` when choosing
or operating a UI driver. Retain all task-derived oracle, layout measurement,
overlay, watchdog, crash/assertion, hang, account, attempt, report, and evidence
rules, with these external-task safety adaptations:

- The performer, not leaves, stages and commits every attempt.
- Overlay code may modify only tracked task-owned source paths. Inventory them
  in `work/test-overlay.paths`; never introduce an untracked source file.
- Save the overlay with `git diff --binary HEAD > work/test-overlay.patch`,
  verify it is nonempty and reapplicable, then restore only inventoried overlay
  paths to `RUN_REF`. Do not run a repository-wide hard reset. After an
  implementation-fix commit, move both `GREEN_REF` and `RUN_REF` to the new
  clean tip before reapplying the overlay.
- Reapply with `git apply --3way`; re-author a conflicting hunk from `test.md`
  rather than leaving conflict markers.
- On locked macOS, force overlay-only testing without waiting or blocking.
  Encode the complete interaction inside the Debug binary using application
  actions or Qt events, log assertions and geometry, capture widgets/windows
  in-process, save the artifacts, and quit. Do not require an OS-level desktop
  screenshot or interactive Computer Use evidence.
- Missing `test_TelegramForcePortable` is the only portable-account setup
  blocker. A `testing` marker file inside the live folder marks it as the
  reusable test copy: marker present means touch no folders and go test. An
  unmarked live folder is real data: move it to real when real is absent;
  delete it only when real already exists. Only then deep-copy golden to live
  and create the `testing` marker inside the copy. There is NO folder cleanup
  after testing — the marked copy stays live for the next run and next task.
  Never delete, rename, move, or alter golden or real.
- Set `RUN_DIR` and `EVIDENCE_DIR` to
  `TASK_DIR/.local/runs/attempt-<n>/run-<m>/`. Promote only decisive compact
  logs/screenshots into tracked `evidence/`.
- Launch every test binary with `-testagent`. Detect crashes from process death
  without `TEST_COMPLETE` plus a new `tdata/working`, not exit code. Read
  captured stderr first, then `tdata/working`, then note the minidump.
- Before each app run and build, stop only a process whose resolved executable
  path equals `EXE`. Never use image-name-wide termination.
- Enforce both the in-app watchdog and an external wall-clock deadline. Count
  test runs independently from implementation attempts and stop at
  `MAX_TEST_RUNS`.
- Plan the fewest possible runs: one complete programmed scenario per attempt
  that proves every check in a single execution, splitting only for checks
  that cannot share one process lifetime. `MAX_TEST_RUNS` is a safety cap,
  never a budget to spend.
- Delete the overlay-bearing Debug executable on every terminal test exit so
  the user cannot launch it accidentally.

The test author must read the full task specification and every current-branch
commit whose message has this task's exact `Task:` line. For an uninterrupted
contiguous run this is the `BASE_REF..GREEN_REF` diff; for a resumed older task,
combine the exact task commits and inspect their current code at `RUN_REF`
without treating intervening tasks as this task's changes. It writes checks
before running, covers every acceptance surface, declares a falsifiable oracle
for each, compresses all checks into the fewest possible runs — normally
exactly one — and never reuses a generic navigate-and-screenshot scenario. Missing
or ambiguous evidence is `TEST_FLAW`; no expected task delta is `IMPL_BUG`. Two
identical consecutive failure signatures block early, except that the macOS
cached-language signature first gets the shared test loop's one-time Xcode
clean-rebuild recovery. A known implementation bug at the attempt cap is
implementation-blocked, not a successful retained commit.

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

A non-`none` `Unverified:` value is routed by that same scheduler step, so write
it to be routable: the exact behavior that shipped without verification, and what
closing that gap would require. That second half is what lets the router separate a
gap a later run can close with the existing setup from one that needs project
infrastructure this checkout does not have. Never widen `Unverified:` to behavior
the task never claimed, and never narrow it to `none` because the acceptance
criteria passed — it records what this run did not prove, not what the task did not
ask for.

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
- A file-lock build error always stops immediately and asks the human to close
  this checkout's Telegram/debugger.
- A locked macOS session and the resulting unavailable Computer Use driver
  never stop or block the task; continue with the complete in-binary overlay
  flow.
- Missing optional screenshots or mockups never block.
- Never silently pass unverified behavior. Surface every blocked or partially
  verified task with exact `work/test.md`, `work/result.md`, and evidence paths.
- In Goal mode, report blocked state without claiming achievement;
  complete the goal only when every selected task is approved.
