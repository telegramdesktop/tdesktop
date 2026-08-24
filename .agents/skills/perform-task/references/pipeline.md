# Complete Telegram Task Pipeline

## Contents

- [Contract and inputs](#contract-and-inputs)
- [Preflight](#preflight)
- [Local artifacts and resumption](#local-artifacts-and-resumption)
- [Delegation](#delegation)
- [Implementation phases](#implementation-phases)
- [Adaptive review and evidence](#adaptive-review-and-evidence)
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

Resolve host kind and the available build trees, commands, executables, and
desktop target as one consistent platform inventory. These are candidates;
assessment selects only the instruments the task needs:

```text
native Windows: cmake --build ./out --config Debug --target Telegram
WSL/Linux:      Telegram/build/docker/centos_env/build_debug.sh
macOS/other:    AGENTS.md and the configured Debug tree

EXE candidates:
out/Debug/Telegram.exe
out/Debug/Telegram
out/Debug/Telegram.app/Contents/MacOS/Telegram

MAX_ATTEMPTS = 4
MAX_TEST_RUNS = 12 per test campaign
MAX_TEST_CAMPAIGNS = 2 total: one normal and at most one focused recovery
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
3. Inspect `TASK_SPEC` for approved source-task prerequisites beyond
   `depends_on`, then run the source-lineage gate before Phase 1:

   ```bash
   python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     source-lineage --source-root SOURCE_ROOT --task TASK_ID \
     [--require EXPLICIT_SOURCE_TASK_ID ...]
   ```

   Require `current_satisfies: true`. A mismatch before Phase 1 returns the
   clean pre-phase routing stop defined below; a mismatch first established
   after Phase 1 follows the task-local Block rule.
4. Run the scripted preflight report and act on its JSON instead of composing
   the equivalent shell checks by hand:

   ```bash
   python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     source-preflight --source-root SOURCE_ROOT --task TASK_ID --exe EXE
   ```

   It reports source/submodule cleanliness, dirty paths outside the owned
   write set, and the golden test account and live marker state for `EXE`.
5. Record available evidence capabilities without requiring them yet: relevant
   compilers and build trees, Docker, unit targets, probe toolchains, Telegram
   Debug executable, golden portable account, and UI driver. After assessment,
   gate every selected instrument before editing or running it. An unavailable
   unselected instrument is irrelevant. An unavailable selected platform or
   stage is either replaced by an equally direct instrument or recorded under
   `Unverified:` with its expected exposure; never silently weaken the oracle.
7. For a new run require a clean tracked Telegram worktree, clean submodules,
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
work/split-proposal.md         # only when assessment rejects intrinsic scope
work/review1-general.md        # mandatory complete-file review
work/review1-<lens>.md         # all five standard lens applicability/reviews
work/review1.md                # canonical first-review verdict
work/review<R>-focused.md      # mandatory focused general re-review, R > 1
work/review<R>-<specialist>.md # only specialists invalidated by the fix
work/review-convergence<C>.md  # bounded loop disposition when needed
work/test-design.md            # assessed evidence contract, reconciled after diff
work/test.md
work/test-cap-assessment-*.md  # independent focused-recovery decision at a campaign cap
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
  implementation unit, initial standard-lens reviews, general review,
  review-fix, and evidence authoring. Every leaf must be told not to delegate and
  never to commit.
- Small-task fast path: the performer may run the context-and-plan checklist
  itself, without a leaf, only when the task spec itself names every file to
  touch and the change is mechanical — roughly two source files or fewer, no
  new APIs, strings, or style tokens, no layout derivation. When in doubt,
  delegate. Assessment always runs as a fresh leaf and has the authority to
  reject the fast-path sizing, reject the plan's whole approach as
  over-engineered, or reject the task itself as several independently testable
  product boundaries. Fast-path and approach rejection force a Phase 1 leaf
  rerun; intrinsic scope rejection writes the split proposal and stops before
  source edits so queue planning can replace the task.
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
message. On Grok Build, follow `.grok/ai-workflow-adapter.md`: blocking
`spawn_subagent` leaves from a top-level performer, same-session checklists
from a `/continue` child, no Codex wait ladder. On Codex, use the
asynchronous wait ladder from the phase prompts: poll no longer than 60
seconds, treat a timeout as not-failure, use artifact mtimes and heartbeat
counters, message the target after five minutes without movement, and
interrupt and retry that disposable phase once after a second unchanged
window. On any host, never replace a live stateful performer.

## Implementation phases

Run sequentially:

1. **Context, visual design, and plan.** One leaf writes a self-contained
   `work/context.md`, then — for `Visual: layout` tasks — `work/visual.md`,
   then `work/plan.md` with exact files, functions, ordered steps, bounded
   phases, owned write sets, adaptive review/evidence plans, the selected
   pre-review validation, and status checkboxes.
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
   than refining it. Before approving the plan, apply the intrinsic-scope gate:
   if it contains several useful boundaries with separate acceptance or
   evidence, stable dependency order, or materially different subsystems and
   failure analysis, write `work/split-proposal.md`, record
   `Scope: split-required`, and stop before source edits. Phase count and line
   count are warning signals, not automatic decisions; the decisive question
   is whether one fresh reviewer and one coherent evidence campaign can judge
   the retained result. On layout tasks verify the visual contract's anchors
   and derivation; on a fast-path plan verify the sizing itself. Require
   `Phases: <N>` and `Assessed: yes`, or a recorded `Fast-Path: rejected` /
   `Approach: rejected` outcome that reruns Phase 1 as a fresh leaf — an
   approach rejection with the assessor's simpler direction as added input —
   or the terminal pre-edit `Scope: split-required` planning boundary.
   This assessor has authority to veto source implementation because it has the
   first exact implementation/evidence plan. It cannot create replacement tasks;
   the performer publishes the proposal as the task's `split-required` result,
   and the scheduler independently owns the deep split transaction. The same
   result may be produced after source edits by the bounded convergence
   assessment when the retained implementation exposes several independently
   shippable/testable boundaries.
3. **Implement.** Run one leaf per assessed plan phase. Before each edit,
   update `work/owned-paths.txt`. A leaf edits only its owned paths and its
   phase status; it does not commit.
4. **Pre-review validation.** Run the assessed fast validation in the
   performer. App source normally builds the configured Debug Telegram target;
   isolated scripts, generators, libraries and harness code may use a focused
   configure, unit, probe or component command. Documentation may defer its
   direct checks to the evidence loop. If a selected build consumes a changed
   resource, force its documented regeneration. Apply exact-path Windows
   cleanup and build-lock recovery only to a command that writes the configured
   build tree.
5. **Review.** Run the adaptive review/fix loop from the phase prompts. Launch
   the first independent general reviewer and all five standard lens reviewers
   against the task and complete diff without sharing findings. The general
   reviewer examines every changed file in full; each lens either proves
   `NOT_APPLICABLE` from the complete diff or performs its relevant focused
   review. General synthesis confirms or drops findings, rejects unsupported
   bailouts, and writes the single `review1.md` the fix phase implements. Only
   material blocking findings cause a fix. After a fix, preserve every prior
   approval that the fix did not invalidate: run one focused general review of
   the fix and affected invariants, plus only the specialists that originated a
   repaired blocker or whose exact review question the fix changed. Presence of
   a broad surface is not enough to replay it. Reconcile only invalidated
   evidence checks and rerun only affected validation. At the convergence
   triggers below, stop the ordinary loop and obtain an explicit repair,
   replan, or rescope disposition.
6. **Normalize.** On native non-WSL Windows, normalize only task-owned source,
   header, style, localization, and build/config text to CRLF without BOM,
   preserving content and trailing-newline state, then rerun the selected
   pre-review validation when normalization could affect it. On macOS,
   Linux, and WSL preserve LF/no-BOM.
7. **Commit and test.** If any review fix followed the last complete selected
   pre-review validation, run that complete validation once after review
   approval; focused per-fix checks do not replace this final gate. When the
   task changed source, create the implementation
   commit with the scripted helper, then run the evidence loop below:

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
   every attempt. When assessment and general review prove the requested
   outcome was already present, leave the source checkout at `BASE_REF`, skip
   `source-commit`, and run the same evidence loop against that state with
   `Outcome: already-satisfied`.

## Adaptive review and evidence

Every unfinished task follows one adaptive implementation path. Task size,
repository location, and whether Telegram is eventually launched do not select
a profile. Before editing, Phase 2 assessment records the task's actual failure
surfaces and writes both of these contracts:

```text
Expected surfaces:
- <surface this task is likely to touch, and the escalation it would imply>

Evidence plan:
- Claim: <acceptance criterion or material shipped invariant>
- Changed surface: <code, build stage, artifact, runtime state, or pixels>
- Instrument: <reading | command | artifact | unit | probe | component |
  telegram-log | overlay | computer-use | screenshot>
- Oracle: <literal pass/fail decision>
- Window: <the mark or bound the reading is taken within>
- Control: <a known-present item the same reading also reaches>
- Falsifier: <the observation that would make this check fail>
- Evidence: <planned durable output>
```

Assessment does not choose among the standard reviewers. `Expected surfaces` is
a recall checklist for their initial applicability scan. Assessment rejects
ceremony as well as under-testing: it removes evidence checks that cannot be
affected by the task.

### Initial review fanout

The initial review always includes one independent **general** reviewer and all
five standard lenses. Start them together when capacity permits; under a slot
limit, keep the complete set queued and start the next lens as soon as a slot
opens. The general reviewer and lenses do not read one another's findings.

The general reviewer reads every changed file in full and owns correctness,
completeness, adjacent integration, unintended regressions, proportionality,
repository conventions, and evidence adequacy. It cannot defer a concern to a
specialist. This is the mandatory safety net.

Each lens first reads the task specification and complete task diff — every
hunk, not necessarily every changed file in full — and chooses one result:

- `NOT_APPLICABLE` — the diff affects no mechanism owned by the lens. Give a
  short, concrete proof tied to the changed paths/hunks, then stop without broad
  repository searches or reading irrelevant files in full.
- `CLEAN` — the lens applies; read the relevant changed files in full and the
  adjacent callers, owners, consumers or precedents needed to decide it, and
  report no material finding.
- `FINDINGS` — perform the same applicable review and report concrete material
  findings.

Uncertainty means the lens is applicable. `Low risk`, small size, time pressure,
and `documentation only` are not proofs of non-applicability. The standard lens
surfaces are:

- **lifetime** — object and resource ownership, callbacks, re-entrancy,
  destruction order, threads, concurrency, races, synchronization,
  cancellation, and shutdown;
- **reuse** — duplication of an established helper, API, style, string, switch,
  algorithm, or mechanism;
- **structure** — cross-module placement, broad moves or deletion, generated
  files, build graphs, platform containment, and new abstractions;
- **performance** — hot or repeated paths, main-thread blocking, startup,
  memory, I/O, scale, and material build-time cost;
- **security** — secrets, authentication, permissions, privacy, cryptography,
  untrusted input, command or subprocess construction, filesystem boundaries,
  downloads, network trust, and destructive behavior.

Lens source search is rooted at `SOURCE_ROOT`; a reviewer must not search or read
`WORK_DIR/review*` or phase-review logs. Accidental exposure is reported and
invalidates only that lens, not already independent work.

The general reviewer may add a named domain specialist when a material risk
such as ABI portability or persistence migration fits none of the five lenses.
Reuse the same general reviewer for synthesis when the host supports continuing
that agent; otherwise a fresh synthesis reviewer starts from the saved general
report and lens reports, then reads only code needed to confirm or drop their
findings. Synthesis rejects a `NOT_APPLICABLE` whose proof contradicts the diff
and sends only that lens back for a full applicable review. It writes the sole
`review1.md` verdict and accounts for all five lens results. Wording, style, or
optional cleanup that does not cause wrong behavior, unsafe use, material
maintenance cost, or a repository-rule violation is non-blocking and never
starts a fix cycle.

### Focused re-review and convergence

An approval remains valid until a later edit changes the exact code or
invariant it established. Before each blocking fix, save the canonical review
and require the fix result to list its touched paths, repaired findings, changed
invariants, and invalidated validations/evidence. After the fix:

1. run one mandatory focused general review over those edits, the containing
   functions and affected callers, and the prior blocking findings;
2. rerun only a lens that originated a repaired blocker or whose prior
   `NOT_APPLICABLE`/`CLEAN` proof the fix actually invalidated;
3. reconcile only evidence checks invalidated by the fix; and
4. carry every other general, specialist, validation, and evidence approval
   forward explicitly.

The focused general reviewer owns synthesis. It reruns a previously inapplicable
lens only when the fix introduces one of its owned mechanisms. Merely touching
an async, storage, build, security, or other broad surface does not invalidate
every lens that could describe it. It reads the complete task diff only when the fix has
made the previous review boundary unreliable, in which case it triggers the
convergence assessment rather than silently restarting the initial review.

Stop the ordinary fix loop and run a fresh independent convergence assessment
when any of these holds:

- two canonical review verdicts have required changes;
- blocker count or affected scope did not materially shrink after a fix;
- a fix adds owned paths, a subsystem, a build/platform boundary, or a new
  ownership, concurrency, persistence, or trust mechanism; or
- the reviewer can no longer state which prior approvals remain valid.

The assessor writes `review-convergence<C>.md` with exactly one disposition:

- `CONTINUE_FOCUSED` — one bounded fix list and at most one final focused
  review round;
- `REPLAN_CURRENT` — the task remains cohesive, but implementation returns to
  an explicitly named validated boundary and a replacement plan before review;
- `RESCOPE_REQUIRED` — the task contains independently testable product
  boundaries; write or update `split-proposal.md` and stop automatic work; or
- `HARD_STOP` — an exact unsafe or unavailable condition needs human action.

If the allowed final focused round still has blocking findings, stop with the
unapproved convergence artifact. Never approve unresolved findings and never
start another broad review campaign merely because time remains.

### Evidence selection

Every task runs the same evidence loop. It may combine instruments:

1. **Static or generated reading** — exact source, configuration, generated
   output, controlled presence/absence, documentation command or link.
2. **Command and artifact** — execute a build/dependency/harness stage; record
   its command, environment, exit code and log; inspect resulting names, sizes,
   architectures, symbols, versions, or options with known-present controls.
3. **Unit, probe, or component** — run an existing suite or a purpose-built
   small binary when it directly exercises isolated code or an ABI without
   requiring Telegram.
4. **Telegram runtime** — build the Debug Telegram target and use task-specific
   log assertions or an overlay when the behavior exists only in the client.
5. **Interaction and visual evidence** — add physical input only when that path
   is the subject, and add tight screenshots plus numeric geometry or raster
   oracles only for visible claims.

Instruments observe through the overlay, the `test/` helpers, existing logs,
or an existing debug seam. Adding new `#ifdef _DEBUG` machinery, debug-only
types, or observation state to production translation units is a review
defect, not evidence (see "Debug-Only Code" in `AGENTS.md`): when
observability is missing, extend the `test/` helpers or the overlay, and
treat any proposed new production debug hook as an exception that needs a
solid, stated reason.

Choose the most direct practical instrument that can detect the negative. The
cheapest sufficient instrument is preferred; a cheaper instrument that bypasses
the changed integration is not sufficient. Checks may use different
instruments in one task. A Telegram executable, portable account, overlay,
desktop driver, screenshot, Docker daemon, or platform toolchain becomes a
precondition only when a selected check needs it.

Use these presumptions unless the assessed task facts justify another direct
instrument:

- app runtime behavior: Debug Telegram build plus instrumented execution and
  literal logs;
- visible text, appearance, or layout: the runtime check plus tight captures,
  with geometry or exact-text assertions;
- build and dependency work: execute the affected stage, inspect its artifacts,
  and build a consumer when consumer compatibility is claimed;
- isolated library behavior: unit suite or standalone probe, with Telegram
  omitted when it adds no coverage;
- deletion or cleanup: controlled absence checks, regeneration, affected
  target build, and retained unit tests; do not replay neighboring network or
  UI flows unless the deletion could change their result;
- harness work: isolated harness self-tests and safety controls, with Telegram
  used only for harness behavior that exists inside the app.

Every check declares a window, a control, and a falsifier before the run. A
check missing any of the three is not run, because each names a way a check
passes or fails without ever reaching its subject:

- **Window** — the mark or bound the reading is taken within. A premise read
  over everything the run has produced answers from rows an earlier stage
  created, and a slice bracketed by wall time collects a slow neighbour's rows.
  Record through `Test::Probe`, take `mark()` immediately before the action, and
  query only `...Since(mark)`; it offers no whole-history accessor.
- **Control** — a known-present item the same reading also reaches. A count
  whose zero is structurally guaranteed measures nothing, and reads as a
  confident absence. Count through `Test::DiscriminatingScan`, which refuses to
  certify a zero the walk cannot tell from absence.
- **Falsifier** — the observation that would make this check fail. A check
  nobody can describe failing is not a check.

Each check must map to an acceptance criterion or a material risk introduced by
the diff, name its falsifier, and retain positive evidence. Apply the revert
test: if reverting the diff could not change the result, the check is
pre-existing behavior and does not belong to this task. For an
`already-satisfied` outcome with no diff, replace the revert test with direct
proof of the requested proposition and why no retained change is warranted.

### Already-satisfied outcome

When inspection finds the requested outcome already present, do not manufacture
a source edit. Assessment records that candidate outcome and designs direct
evidence. The mandatory general review independently confirms the relevant
current code and the evidence contract; specialists run only when the existing
subject needs their risk-specific judgement. Run the ordinary evidence loop
against `RUN_REF == BASE_REF`.

Approval without a source commit requires all of:

- `Outcome: already-satisfied` and `Touched: none` in `work/result.md`;
- no `GREEN_REF`, a clean source checkout at `BASE_REF`, and no owned source
  changes;
- an approved general review and passing evidence for every acceptance
  criterion.

If measurement instead exposes a deviation, implement the repair in this same
task, reassess changed surfaces, review, commit, and rerun affected checks.

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

Determine the prefix separately for each retained commit, not once for the
whole task and not from the temporary test overlay. Start the subject with
exactly `[ai] ` only when every retained change in that commit, and the commit's
purpose, are exclusively about the AI workflow: the agent harness, skills,
prompts, custom commands, agent documentation, or AI testing infrastructure.
Typical qualifying paths include `Telegram/SourceFiles/test/`, `.agents/`,
`.claude/`, `.grok/`, `AGENTS.md`, `CLAUDE.md`, and `GROK.md`, but paths alone
do not decide the prefix. Product-specific test seams, app code, and build-system
integration do not qualify merely because agents use them for verification.
Split mixed workflow and product work into separate implementation commits when
practical; otherwise the mixed commit must not use `[ai] `. The disposable test
overlay and external AI task artifacts do not count. Every other commit must not
contain `[ai]` anywhere. The prefix counts toward the subject length.

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

## Evidence loop adapter

Read `.agents/shared/test-loop.md` completely and enter its universal evidence
loop after the first green implementation commit, or after general review when
the outcome is `already-satisfied`. Start from the assessed
`work/test-design.md`, reconcile every check against the final diff or the
proved no-change proposition, and write `work/test.md` before executing any
check.

The performer, not leaves, stages and commits every implementation attempt.
Use `TASK_DIR/.local/runs/attempt-<n>/run-<m>/` for complete logs, probes,
screenshots, dumps, and command by-products. Promote only compact decisive
evidence. A command without its exact command line, working directory,
environment additions, exit code, and complete local log did not run. Every
artifact or absence assertion includes a known-present control when a mistyped
path or pattern could otherwise pass.

A run may execute several instruments. Pack checks that share setup, but do not
force unrelated commands into one shell process or force all runtime checks
into one application lifetime. Count one evidence run for one planned execution
set recorded under a run directory. Every rerun keeps prior positive evidence
and executes only failed or invalidated checks.

Before an instrument runs, gate only its own prerequisites:

- command, unit, probe, component, and artifact checks need their named
  toolchain and isolated output location;
- a Telegram build needs the matching configured Debug tree;
- a Telegram launch additionally needs the exact executable, golden portable
  account, safe path-scoped process control, and test harness;
- Computer Use needs the separate capability gate;
- another platform's unavailable toolchain is recorded as an exact
  `Unverified:` exposure, not simulated by an unrelated local command.

When a Telegram overlay or app launch is selected, read
`Telegram/SourceFiles/test/README.md` completely and then the chosen helper
headers. Read `references/computer-use-testing.md` when selecting or operating
a UI driver. Retain all shared task-derived oracle, layout measurement,
watchdog, crash/assertion, hang, account, and evidence rules, with these
external-task adaptations:

- Prefer an overlay in
  `Telegram/SourceFiles/test/test_scenario.cpp`, but place disposable probes
  or direct entry points in any relevant tracked source or initialized
  submodule when that is more direct. Inventory every path in
  `work/test-overlay.paths`; never add an untracked source file or commit the
  overlay.
- Save and restore an overlay through the helpers:

  ```bash
  python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
    overlay-save --source-root SOURCE_ROOT --task TASK_ID --restore run
  python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
    overlay-apply --source-root SOURCE_ROOT --task TASK_ID
  ```

  Save before restore. Reapply only after an implementation-fix commit; reauthor
  a conflicting hunk from `test.md`.
- Execute every app run through:

  ```bash
  python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
    test-run --exe EXE --run-dir RUN_DIR [--env NAME=VALUE ...] \
    [--deadline 120] [--quiet 60]
  ```

  The helper owns portable-account setup, exact-path process cleanup,
  `-testagent -noupdate`, stale-crash relocation, watchdogs, stdout/stderr,
  markers, and crash collection. It gathers; the performer assesses.
- A pre-Runner crash or DeadlockDetector event is a production startup failure,
  not automatically a fixture or harness failure. Follow `shared/test-loop.md`:
  symbolize a usable dump, and after at most one identical run use its Windows
  live-debugger fallback when the dump is empty, unreadable, or lacks the main
  thread stack. Do not reset the account merely because `launch_finished` was
  never reached.
- Missing `test_TelegramForcePortable` blocks only a selected Telegram launch.
  Never delete, rename, move, or alter the golden or preserved real account.
  Reuse a marked live test copy under the shared account rules.
- On locked macOS, replace external driving with the complete in-binary overlay
  flow. The lock is never a test block and does not reduce logged, geometry, or
  in-process capture coverage.
- Use Computer Use only when physical input is the subject. Most UI checks stay
  overlay-driven; visible claims still require tight in-process captures.
- On terminal exit from an overlay run, call
  `test-cleanup --exe EXE --delete-exe` so no overlay-bearing executable
  survives. Direct command, artifact, unit, or probe runs do not delete an
  unrelated Telegram executable.

For every instrument, assessment returns exactly one of:

- `APPROVED` — every selected check has positive evidence;
- `TEST_FLAW` — the command, fixture, probe, oracle, capture, or evidence
  collection was incapable of deciding the claim;
- `IMPL_BUG` — a sound check exposed a defect in the retained implementation;
- `UNRECOVERABLE` — a required subject or capability cannot be reached safely
  after the bounded directness assessment.

A `TEST_FLAW` recovery must remove an assumption or move closer to the changed
surface. Do not repeat the same command, fixture, or overlay wording. The normal
campaign cap is an assessment checkpoint: carry prior passes, isolate unmet
checks, and choose at most one focused recovery campaign or document recovery
exhaustion. A focused campaign that does not converge stops automatic work with
an exact recoverable hard stop; it never starts a third campaign or converts the
cap into approval/Block. An `IMPL_BUG` fix creates a new retained attempt, uses the same
focused general/invalidated-specialist review and convergence triggers as the
pre-commit loop, and reruns only invalidated checks. It never restarts the
initial complete-diff review simply because the attempt number changed.

The evidence author reads the full task, assessed plan, final task diff, and
existing `test-design.md`. It covers every acceptance surface and nothing
outside it. A check discovered late is taken now when this checkout has the
required capability; a check that cannot be affected by the diff is removed
rather than exported as coverage debt. Use an out-of-scope fence through
`fence-create` and `fence-check` when the task needs one.

## Final AI state

Before publishing an approved result or genuine blocked boundary, require a
clean Telegram checkout at `RUN_REF`, with `GREEN_REF` in its history when an
implementation is retained, no overlay in source, and no overlay-bearing
executable. The marked live test copy stays in place per the test-loop folder
rules. For implementation-blocked work with no
retained commit, restore only proven owned paths to `BASE_REF`. For test-blocked
work retain the latest implementation commit and state the exact unverified
behavior. `Blocker-Type: test` additionally requires `work/test.md` to contain
`## Recovery exhaustion`, unless the verdict is the separately documented
Computer Use infrastructure-unavailable case. A `TEST_FLAW`, a run cap, or a
missing capture can never be the blocked verdict.

For `split-required`, do not clean, reset, stash, or checkpoint retained source
implementation. Require current task base/run refs, keep the owned diff and any
green commit in place, and inventory every owned path in `work/owned-paths.txt`.
The publication helper seals that work in `work/carried-work.json`; the later
split transaction rechecks the seal before assigning an implementation carrier.

Write `work/result.md` with exactly one value for every field:

```text
# Task result: <TASK_ID>
STATUS: DONE | BLOCKED | SPLIT_REQUIRED
Outcome: changed | already-satisfied | blocked | split-required
Verdict: APPROVED | SPLIT_REQUIRED | <specific blocker>
Blocker-Type: none | test | impl | unrecoverable
Implementation: retained | none
Split-Proposal: work/split-proposal.md | none
Attempts: <n>
Test-Runs: <n>
UI-Driver: overlay | hybrid | mixed | hybrid-unavailable | not-applicable
Touched: <source paths or none>
Test-Report: work/test.md
Evidence: <tracked evidence paths and what they prove>
Unverified: none | <exact behavior and manual follow-up>
Checkout: clean-buildable | source-state-retained | unsafe
Discovered: none | present

## Discovered tasks
<complete independently testable follow-ups, or omit>
```

`Outcome: changed` requires a retained task implementation and names its paths
under `Touched:`. `Outcome: already-satisfied` requires `Touched: none`, no
source commit, and direct evidence that the requested proposition held before
the task. A blocked result uses `Outcome: blocked` and retains a latest safe
implementation attempt when one exists.

A split result uses `STATUS: SPLIT_REQUIRED`, `Outcome: split-required`,
`Verdict: SPLIT_REQUIRED`, `Blocker-Type: none`, and
`Split-Proposal: work/split-proposal.md`. Set `Implementation: retained` and
name its paths under `Touched:` when owned source work exists; otherwise use
`Implementation: none` and `Touched: none`. It uses
`Checkout: source-state-retained` and does not require a test report, because
each replacement receives its own complete review and evidence campaign.

For approved project work, promote `work/project.proposed.md` to the project's
`project.md` immediately before final AI publication. For blocked work, retain
the proposal only as a task artifact.

Publish final AI state only after the Telegram commit and result are final:

```bash
python3 SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
  finish --source-root SOURCE_ROOT --task TASK_ID \
  --status approved|blocked|split-required --model MODEL_SHORT_NAME
```

`--model` is required and records which model finished the task, into the
`model` field of its `state.yaml`. Self-report the model you are actually
running as, as a lowercase short name — `claude-opus-5`, `claude-fable-5`,
`gpt-5.6-sol`, `glm-5.3`, `kimi-k3`, `grok-4.6`. Report the model running the
performer that reaches this boundary, not a leaf's model and not whichever model
happened to start the task: the field answers "who finished it", so a task
resumed by a different model after an interruption records the model that
actually completed it. Never guess or copy the value from another task; if you
cannot tell what you are, say so and stop rather than recording a wrong name.

For approval or Block, the helper verifies a clean source checkout, local task
refs, current `HEAD`, and either the retained implementation's exact three-line
commit message or the strict no-change state required by `already-satisfied`.
For split-required it verifies current refs, seals the owned working state, and
does not require a clean source checkout. It commits all task-scoped local
artifacts and final state as `Approve <TASK_ID>`, exceptional
`Block <TASK_ID>`, or `Split-required <TASK_ID>`, fetches newer canonical state when configured,
rebases the slot, publishes without force, and fast-forwards local AI master.
It deletes all local task refs after approval; after a block it deletes only
`RUN_REF` and retains implementation recovery refs for the next invocation;
after split-required it retains all refs for the scheduler's carrier transfer.
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
needs another platform or architecture, a second account, funded external
value, real server-backed cloud state, a purpose-built bot, or hardware this
machine does not have. Write it to be
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

- `Scope: split-required` before source edits and `RESCOPE_REQUIRED` from a
  convergence assessment are planning boundaries, not task `Block` verdicts
  and not permission to keep retrying. Preserve `split-proposal.md`, all owned
  implementation and source recovery, write the split result, and publish
  `finish --status split-required`; then return control to the scheduler (or
  human for a direct invocation). Assessment owns the stop; only the scheduler
  owns task creation, dependency rewrites, implementation-carrier transfer,
  and retiring the original. A performer resolving an already published
  split-required task reports it immediately without rerunning context,
  implementation, review, or builds.
- Source lineage has a strict timing boundary. Before Phase 1, a missing
  approved prerequisite is a clean pre-phase routing stop: do not publish
  `blocked`, edit source, or create a backport/cherry-pick/rebase/merge task.
  The scheduler may switch to a compatible existing branch and resume. If the
  missing prerequisite is first established after Phase 1 completed, restore
  owned and disposable changes, publish a clean task-local `blocked` boundary
  naming the missing source task and branch evidence, and let `continue` run
  non-dependent batch work. Never perform branch integration inside the task.
- A disposable phase may be retried once through the wait ladder. Never fresh
  retry the performer within the same attempt. An interruption leaves local
  task state `in-progress`; a later `continue` invocation resumes it. A later
  invocation reopens a published blocked task locally without a `Resume`
  commit.
- A clean `blocked` attempt leaves the task unfinished. It lets `continue`
  proceed with independent work, but the next invocation retries it once before
  starting new shared work. A dirty/non-buildable checkout or global
  environment problem stops the current invocation.
- A test campaign cap is not a clean blocked attempt. Preserve the overlay and
  evidence and run the cap assessment. The normal campaign may authorize one
  focused campaign; the focused campaign boundary either proves recovery
  exhaustion or returns a recoverable hard stop instead of looping. The
  publication helper rejects a test block whose report still says `TEST_FLAW`,
  cites the run cap, or lacks the required recovery-exhaustion record.
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
