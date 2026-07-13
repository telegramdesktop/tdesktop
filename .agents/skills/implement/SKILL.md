---
name: implement
description: Autonomously implement and verify Telegram Desktop changes from an inline request, task-list path, or a prepared project task source under .ai, with or without mockups. Use when Codex should split work into independently testable tasks and drive each through context, planning, implementation, a Debug build, one review pass, artifact-grounded in-app testing with optional Computer Use, resumable artifacts, a lean parent task, and native-Windows CRLF normalization.
---

# Implement Pipeline

You are the top orchestrator. Normalize an inline description or task-list file into a project with
a testability-split task list, then drive each task to test-approval through an isolated per-task
**task-runner**. Keep only the task list and one compact summary per task in the parent. Heavy work
happens in the disposable runner and, when nested delegation is available, fresh leaf phase agents.

This tested superset of `task-think` does not re-specify the implementation phases or the test
loop. Read and reuse:
- `.agents/skills/task-think/PROMPTS.md` — Phase 0-6 prompt templates and the Codex execution-mode
  / wait-ladder / progress-heartbeat / compact-reply rules.
- `.agents/shared/test-loop.md` — the harness-neutral impl⇄test loop (state machine, handoff,
  overlay, account swap, watchdog); `references/computer-use-testing.md` adds a Codex-only UI driver.

### Input sufficiency and visual evidence

Treat the task description and repository as sufficient. Visual references are optional unless expressly required; absence alone never causes a stop, question, malformed task, or block.
Use, in order: request facts; supplied references; adjacent UI/code/styles and baseline; repository
history/legacy; then the closest desktop convention and smallest common-sense change. Record
assumptions; never invent a reference. Block only for expressly required exact content or bytes that
cannot be recovered, and continue every independent task.

## Inputs

Set `REQUEST` to the invoking user text, including attached-image references; skills do not receive
the deprecated custom-prompt `$ARGUMENTS` macro. `REQUEST` is one of:
- an inline task description (e.g. `add a dark-mode toggle to settings`)
- a path to a task-list file (e.g. `.ai/communities/tasks.txt` -- a rough list of tasks to refine)
- an existing project name to resume, optionally followed by extra work
- **just a project name with a prepared `.ai/<project>/tasks/about.md`** -- the default task source.
  With no other input, `implement <project>` runs it (see Artifacts).

## Config

Run in the **current checkout** without creating a worktree. Resolve platform, build tree, command,
and executable together; never mix native-Windows commands with a WSL tree.

```
NATIVE_WINDOWS_BUILD = cmake --build ./out --config Debug --target Telegram
WSL_BUILD            = Telegram/build/docker/centos_env/build_debug.sh
EXE_CANDIDATES       = out/Debug/Telegram.exe | out/Debug/Telegram | out/Debug/Telegram.app/Contents/MacOS/Telegram
COMPUTER_USE_APP_TARGET = Windows: absolute EXE | macOS: absolute outer .app containing EXE | other: none unless supported
TEST_ACCOUNT         = out/Debug/test_TelegramForcePortable
MAX_ATTEMPTS         = 4
MAX_TEST_RUNS        = 12
COMPUTER_USE_POLICY  = auto | overlay-only | required (default auto; user request overrides)
SUBAGENT_QUALITY     = inherit the parent task's selected model and reasoning effort
```

Follow `AGENTS.md` if it names a different command. Build Debug only. Verify `EXE` from the actual
tree. Scope proactive cleanup to its resolved full path; never kill processes by image name.

The test binary is **always launched with `-testagent`** (test-loop.md "Crashes & assertions"). It
suppresses modal assertion dialogs, turns assertions and a frozen main thread into a crash with a
`tdata/working` report, and writes assertion text to captured stderr. Detect crashes from the report,
not the exit code.

Keep the parent model/reasoning selection for all subagents; a GPT-5.6 Sol Ultra parent therefore
keeps that quality. Do not invent model, reasoning, or role fields missing from `spawn_agent`. If a
host exposes overrides, match the parent. Custom agents use `model_reasoning_effort`.

### Codex collaboration contract

- Use `spawn_agent` with a unique lowercase/digit/underscore `task_name`; save its canonical target.
- Use `fork_turns: "none"` with self-contained prompts; fork minimal turns only for thread-only context.
- The top orchestrator spawns the task-runner; the runner selects NESTED only after its first real
  phase-leaf spawn succeeds. An immediate depth/capacity/policy rejection before phase work selects
  SAME-RUNNER; then execute the same prompt checklists locally. Never switch modes for a wait timeout.
- Every delegated planner or phase worker is a leaf and must not spawn more agents, especially under
  Ultra's proactive delegation. Keep implementation phases sequential unless their plan proves
  disjoint write sets and the current checkout has safe capacity.
- `wait_agent` can wake for any agent or new user input, not just the intended target. After every
  wake, inspect the saved target with `list_agents` and validate its artifact. Follow the detailed
  wait/retry contract in `task-think/PROMPTS.md`.
- Never duplicate a task-runner. It owns stateful writes, commits, and test attempts.

Tasks run **sequentially** in this one checkout (the build cache stays warm; app runs must serialize
against the account anyway). To parallelize, run the skill in a different checkout/slot (e.g.
`C:\Telegram\tdesktop`, `D:\Telegram\tdesktop`, `D:\Telegram\twin`). Each run is independent and
single-tree. Never run the **test phase** in two slots against the same account at once; concurrent
clients on one auth key can trigger a session reset, so give parallel slots separate test accounts.

## Artifacts (per project)

- `.ai/<project>/tasks/about.md` — the **default task source**: a human-prepared description or rough
  list, with optional mockups beside it. The planner reads it for `implement <project>`. It is distinct
  from the project blueprint `.ai/<project>/about.md`
  (the `tasks/` subdir is what disambiguates them).
- `.ai/<project>/implementing.md` — the canonical, final, testability-split task list. The planner
  creates or rewrites it in Phase B; after the main thread adopts it, only the main thread edits it.
- `.ai/<project>/images/` — illustrations referenced by tasks (`images/01.png`, ...).
- `.ai/<project>/<letter>/` — task context, plan, visual contract, review, test, result, overlay, logs.
- `.ai/<project>/about.md` — project blueprint (task-think convention).

## Terminal state and Goal mode

The pipeline is terminal when every task is `approved` or `blocked: <reason>`, but successful only
when all are approved. A blocked task is a loud terminal result, not goal achievement.

If a Goal mode objective is active, do not create or replace it. Complete it only when every task is
approved; use blocked-state rules only when their threshold is satisfied. Reinvocation continues
`todo` or `in-progress` tasks; a blocked task stays terminal until explicitly requeued or replaced.

## Phase A: Setup & input resolution (main thread)

1. Record `START_TIME` with the host's current-time facility.
2. Detect native Windows vs WSL/Linux vs macOS/other; read `AGENTS.md`; resolve `BUILD`, `EXE`,
   `TEST_ACCOUNT`, `COMPUTER_USE_APP_TARGET`, `COMPUTER_USE_POLICY`, and the active Computer Use
   skill path (or `none`). On macOS require EXE to realpath under `<target>.app/Contents/MacOS/`, never
   use the inner binary as the app target. Strip only a driver-policy directive from `REQUEST`. On WSL use Docker
   and LF/no-BOM; on native Windows use the configured Debug tree and later CRLF phase.
   Verify path-scoped process control, safe folder ops, launch/capture, and a usable app-run display
   (WSLg/Xvfb counts) or stop. Never build Release; Computer Use capability is separate and optional.
3. **Test-account gate (hard precondition — before planning or implementation).** If
   `out/Debug/test_TelegramForcePortable` does not exist, STOP the entire skill immediately and tell
   the user that the test account is not prepared: create `out/Debug/test_TelegramForcePortable`
   (a portable-data folder authed to a throwaway test account) before `implement` can run, because
   autonomous testing is impossible without it. Do no implementation work.
4. **Clean-checkout gate.** Require a clean tracked worktree and clean submodules before the first
   planner or runner spawn. Ignored `.ai/` artifacts are allowed. If unrelated tracked, staged,
   untracked, or submodule changes exist, stop without stashing, committing, or resetting them.
   Record `BASE_SHA`; invocation authorizes destructive resets only for changes proven to belong to
   this workflow.
5. **Resolve `REQUEST` into (project, SOURCE, mode) — without reading task files or images.**
   The main thread never loads task prose or assets; resolving needs only paths and existence checks.
   SOURCE ends as EITHER inline text OR a confirmed file path that the planner will read.
   - **File input** — if `REQUEST` as a whole or its first quoted token resolves to an existing path,
     confirm existence without reading it and set SOURCE to that path. If it is under `.ai/<name>/`,
     project = `<name>`; otherwise derive a short kebab name from the filename. Mode = **extend** if
     that project already has `implementing.md`, else new.
   - **Existing project** — else if `.ai/<FIRST_TOKEN>/` exists: project = `FIRST_TOKEN`.
     - If there is a **remainder**: if it is a path to an existing file, SOURCE = that path (confirm
       it exists, do NOT read it); otherwise SOURCE = the remainder text. Mode = **extend** only when
       `implementing.md` exists; otherwise mode = **new** within this existing project directory.
     - If the remainder is **empty**, resolve SOURCE in this priority order (existence checks only,
       do NOT read):
       1. If `.ai/<project>/tasks/about.md` exists → SOURCE = that file (the **default task
          source**); mode = **extend** if `implementing.md` already exists, else **new**. This is the
          `implement <project>` with a prepared task source path — it fires the full pipeline.
       2. Else if `implementing.md` exists → mode = **resume** (no SOURCE; Phase C finishes the
          still-unfinished tasks).
       3. Else there is nothing to implement — tell the user to prepare `.ai/<project>/tasks/about.md`
          (or pass a description / task-file path) and stop.
   - **New inline** — else SOURCE = all of `REQUEST`; pick a unique short kebab-case project name
     after consulting `.ai/`.
   After this step you always have a project name and either a SOURCE (inline text or a confirmed
   path) or mode = **resume** — and you have read neither the file nor any image.
   Set `FIRST_TASK_ID` after a narrow heading scan to the next id after the union of task headings
   in `implementing.md` and artifact directories (`a`...`z`, `aa`...); never reuse an id.
6. Create `.ai/<project>/` and `.ai/<project>/images/` if new.
7. **Persist supplied visual inputs when present.** Prefer SOURCE or project images. For a chat-only
   attachment, fork the smallest necessary turn window and persist its description; ask for an
   on-disk copy only when exact unavailable bytes are required. With no image, continue normally.
   Never claim a chat-only image was saved; the planner copies filesystem-visible references.
8. If mode = **resume**, skip Phase B and go to Phase C.

## Phase B: Planning & testability split (delegate)

Spawn one planner with a unique tool-valid task name and `fork_turns: "none"`, except for the
smallest recent-turn fork explicitly selected in Phase A for a chat-only visual. It inherits the
parent quality setting and is a leaf: it must not delegate. Use this prompt shape:

```
You are a planning/splitting agent for a large C++ codebase (Telegram Desktop).
You are a leaf worker. Do not spawn or delegate to other agents.

SOURCE — EITHER an inline request OR a path to a task-list file. If it is a PATH, READ it yourself
(and any task files it points to); the main thread has NOT read it. If it is inline text, use it as
the request:
<the inline description, or the file path>
PROJECT: <project>     MODE: <new | extend>     FIRST_TASK_ID: <next unused id>

IMAGES (optional) — resolve SOURCE-referenced paths relative to its directory; siblings of
`.ai/<project>/tasks/about.md` are candidates. READ each referenced image, COPY it into
`.ai/<project>/images/` with a descriptive name, and attach it to every pertinent task. The main
thread did not read or move it. Treat a chat-only textual description as visual evidence:
<description(s) or none>. With none, continue from the request and repository; never ask for a
mockup or weaken, omit, or block a visual task solely for lacking optional references. The express
exact-content exception in "Input sufficiency and visual evidence" still applies.

Read AGENTS.md. Briefly scan the codebase to gauge scope. Produce the FINAL ordered task list that
satisfies BOTH constraints for every task:

- **Implementable in one pass**: a fresh implementation agent must be able to complete the task with
  comfortable context headroom and without relying on compaction — a bounded change across a
  handful of related files, not a sweep across dozens. If a unit is too big, split it.
- **Independently testable**: each task must yield an observable behavior the test agent can drive
  from an in-app debug overlay and verify via log/screenshot. Split on testable seams, so each task
  ends at a point where something concrete can be exercised and checked.

Use the minimal number of tasks subject to both constraints; preserve dependency order (a task comes
before any task that depends on it). If the SOURCE is already a list, respect its intended breakdown
and refine only as needed: split entries that are too big or not independently testable; you may
merge trivially tiny adjacent entries if the result is still one testable unit.

Write `.ai/<project>/implementing.md` in EXACTLY this format:

# Implementing: <project>

## Goal
<one-line overall goal>

## Tasks

### <FIRST_TASK_ID>: <imperative title>
Status: todo
<2-4 line self-contained description: what to implement and the observable, testable result. Enough
that a fresh agent can act on it.>
Depends-On: none | <comma-separated earlier task ids>
Observable: <specific runtime evidence that proves this task works>
Visual: layout | appearance       (user-visible visual/asset changes only; omit otherwise)
Design-Basis: <ordered request/image/current/legacy/repository evidence and assumptions; visual tasks only>
Images: images/<file> — <caption>      (this line only if the task uses an image)

### <next id>: <imperative title>
Status: todo
<...>

**Images per task (required when supplied).** Attach every pertinent supplied image via `Images:`,
with a precise caption, and account for unused ones. With none on a visual task, omit `Images:`, cite
non-image evidence in `Design-Basis:`, and never create a placeholder. Non-visual tasks omit both.

**Visual classification (required for visual/asset changes).** For every task that changes how
user-visible UI, rendered output, or an asset looks, add `Visual:`; it routes the task-runner:
- `Visual: layout` — reproduce composition: element sizes, proportions, spacing, margins,
  alignment, or component geometry. This triggers a dedicated design-spec phase and a
  geometry-measuring oracle whether or not a mockup exists.
- `Visual: appearance` — match color, wording, style choice, or glyph identity without changing
  proportions or geometry. This uses the lighter visual comparison without a numeric contract.
- Omit `Visual:` only when the task changes no appearance.
Classify from the requested change, never from reference availability. When uncertain, use `layout`
for anything composed from multiple sized or positioned pieces. The user may override the line.

Every task must include `Depends-On:` and `Observable:`. Dependencies may name only earlier tasks.
Use `Depends-On: none` when the task can still run after any earlier task is blocked. The observable
must name the exact log value, action/state transition, or tightly framed visual evidence the test
will verify; "screen opens" is not sufficient. Every task with `Visual:` must also include a
`Design-Basis:`; supplied images are one possible basis, not a prerequisite.

Use spreadsheet-style ids a...z, aa... without reusing an existing task artifact id. Do not plan internals or implement. When done, reply with ONLY a
compact confirmation — `ready — <N> tasks` (extend: `ready — appended <letters>`); do NOT echo the
task list or image contents back, the main thread reads `implementing.md` itself.
```

For **extend** mode, instead instruct the planner to FIRST read the existing `implementing.md`, then
rewrite it as: (1) a TRIMMED completed-history — keep only the **three most recent** `Status: approved`
task blocks (the three nearest the bottom of the file) and drop all earlier approved ones; (2) every
still-unfinished task left untouched, in place and with its status — that is all `todo`, `in-progress`,
and `blocked` blocks (never drop these); then (3) APPEND new tasks starting at FIRST_TASK_ID after
them. FIRST_TASK_ID follows the pre-trim task-heading/artifact union. The trim only removes
already-approved entries from the list — it never touches the per-task `.ai/<project>/<letter>/`
artifacts on disk, so a follow-up letter can still read an earlier letter's `context.md` even after its
block was trimmed out of `implementing.md`. It must append only work from SOURCE not already
represented either in `implementing.md` or in the preserved per-task `context.md`, `result.md`, and
test artifacts. Deduplication must include trimmed history, so re-running `implement <project>`
against an unchanged default `tasks/about.md` appends nothing (the planner replies
`ready — appended (none)`, still applying the completed-history trim). Any
`todo`/`in-progress` leftovers from an interrupted run are picked up by Phase C regardless, so
defaulting to extend never loses an in-flight batch; it is a superset of resume.

After the planner replies `ready`, read `implementing.md` once (the first and only load of task
prose; never read images) and initialize a progress list mirroring the tasks.

For **resume**, read and validate `implementing.md` once here before Phase C and initialize the same
progress list. Treat any status line beginning with `Status: approved` or `Status: blocked` as the
corresponding legacy terminal state, then normalize it to the canonical grammar the next time the
main thread edits that block. For a legacy unfinished block without `Depends-On:` or `Observable:`,
assume `Depends-On: none` and use its self-contained result sentence as the observable rather than
blocking resume on a format migration.

## Phase C: Per-task loop (main thread orchestrates)

For each task whose normalized `Status` is neither `approved` nor `blocked`, in order:

1. If any id in `Depends-On:` is blocked, do not spawn a runner. Set
   `Status: blocked: prerequisite <ids> blocked`, record the missing behavior, and continue to the
   next independent task. The main thread creates its canonical `result.md` with `STATUS: BLOCKED`,
   `Blocker-Type: impl`, HEAD as base, no implementation/test, and prerequisite results as evidence.
2. Record `TASK_BASE_SHA = HEAD`, set `Status: in-progress`, and mark the progress item in progress.
   Spawn exactly one **task-runner** with a unique task name and `fork_turns: "none"`, using the
   prompt below. It inherits the parent model and reasoning selection.
3. Poll with waits no longer than 60 seconds. Each wake may belong to another agent or user input;
   check the runner's canonical target and its progress/result artifacts. Use `send_message` while
   it is running and `followup_task` if it is idle but owes the final result. Never fresh-retry the
   whole runner. If it becomes irrecoverably unresponsive, interrupt it, audit the checkout and
   artifacts, and classify the task as blocked only if the tree is clean and buildable; otherwise
   hard-stop.
4. Validate `<TASK_DIR>/result.md`, its referenced commit, `test.md` when applicable, cleanup state,
   and a clean worktree. The compact reply is a notification, not proof. Update the canonical task
   status to `approved` only for a validated `STATUS: DONE`; otherwise write
   `blocked: <specific reason>`.
5. When `Discovered: present`, accept ordered blocks headed `discovered-1`, `discovered-2`, etc. Rescan
   current task headings and artifact dirs, assign unused spreadsheet ids, and rewrite earlier placeholder
   dependencies before appending. Reject collisions/forward dependencies; send malformed blocks through a leaf planner. Only the main thread assigns ids and writes
   `implementing.md` after Phase B.
6. On BLOCKED, **do NOT stop the loop — prioritize continuing development.** This often runs
   unattended for hours, so NEVER pause to ask the user whether to go on; record the blocker and
   move to the next task as long as further progress is possible:
   - **Test-blocked** — the runner committed a building impl and only its in-app verification could
     not complete (a harness limit, or the attempt cap hit on a test flaw, not a real bug). The code
     is on disk, so CONTINUE; capture EXACTLY what was left unverified for the loud final report.
   - **Impl-blocked but checkout clean** — no green impl for this task, but HEAD is left at a prior
     committed, buildable commit. CONTINUE — later tasks may be independent; record the missing
     behavior.
   - **Hard stop ONLY when continuing is truly impossible** — a broken / uncommitted / non-buildable
     checkout, or a global environment failure (file lock needing the user to close `Telegram.exe`,
     the test-account gate). Only then stop and report.
   Before spawning the next task, confirm the tree is clean and HEAD is a known buildable commit.
   Never reset an unexpected or unrelated path. If the runner cannot prove and restore only its own
   changes to a known-good SHA, hard-stop. Every blocked/unverified task must be surfaced loudly in
   Completion with its exact `<TASK_DIR>/test.md` or `<TASK_DIR>/result.md` path.

### task-runner prompt

````
You are a task-runner for ONE task in an autonomous implement-and-test workflow on Telegram
Desktop (C++ / Qt). You own this stateful task end to end; no second runner may operate on it.
Inherit the parent model and reasoning setting. At startup, select one execution mode for the task:
- NESTED: choose only after the first real phase-leaf spawn succeeds. Give every leaf a unique
  tool-valid name, `fork_turns: "none"`, and an instruction not to delegate.
- SAME-RUNNER: choose if that first spawn is immediately rejected by depth, capacity, or policy;
  execute the same task-think prompts as strict checklists. This is not degraded failure.
After selection, do not switch modes merely because a wait timed out.
PROJECT: <project>   TASK: <letter> — <title>
TASK DESCRIPTION:
<the task's full description block from implementing.md>
IMAGES: <referenced .ai/<project>/images/* paths, or none — Read them if present>
TASK_DIR: .ai/<project>/<letter>/   TASK_ID: <project>-<letter>
TASK_BASE_SHA: <HEAD before this runner was spawned>
HOST_KIND: <native-windows | wsl-linux | macos | other>
Config: BUILD=<value>; EXE=<absolute executable>; COMPUTER_USE_APP_TARGET=<absolute outer .app | absolute Windows EXE | none>; MAX_ATTEMPTS/MAX_TEST_RUNS=<values>; COMPUTER_USE_POLICY=<value>; COMPUTER_USE_SKILL=<active path | none>.
Read first: AGENTS.md; REVIEW.md; `.agents/skills/task-think/PROMPTS.md` (Phase 1-6 templates +
execution rules); `.agents/shared/test-loop.md` (testing). Read any IMAGES listed above. For a
follow-up letter, also read `.ai/<project>/about.md` and the nearest earlier task `context.md` that
exists; prerequisite-blocked tasks may have none. Within
task-think instructions, "main/current session" means this runner, not the orchestrator.
Treat `IMAGES: none` as normal; missing mockups alone never justify pausing, blocking, or asking.
Create `<TASK_DIR>/` and `<TASK_DIR>/logs/`. Maintain
`<TASK_DIR>/logs/task-runner.progress.md` at phase boundaries so the orchestrator can distinguish a
long phase from a stalled runner.
This wrapper overrides shared commit ownership: leaf workers never commit; the runner stages exact
owned paths and commits without `git add -A`. Safety rules below replace conflicting generic reset,
account, and file-lock mechanics.
Pipeline for THIS task only, writing prompt/progress/result logs per task-think:
1. CONTEXT  — use Phase 1F whenever earlier project context exists, including the first
   `implementing.md` batch in an older task-think project; otherwise use Phase 1. Preserve the
   current `about.md` if present, then let the
   context phase write its future-looking blueprint, then move that new file to
   `<TASK_DIR>/about.proposed.md` and restore the prior project blueprint (or leave it absent for a
   new project). Current downstream phases use context.md, not the proposed blueprint. Promote
   about.proposed.md to the project `about.md` only after this task is approved; a blocked task must
   not make future follow-ups believe missing behavior exists.
1b. DESIGN-SPEC — only for `Visual: layout`. Inventory `Design-Basis:`: read supplied images, if any,
   then inspect current/legacy implementations and closest desktop widgets/style tokens. Write
   `<TASK_DIR>/visual.md` with cited evidence, assumptions, and test-loop.md's ordered derivation. Ground
   every dimension in a font metric, style token, sibling geometry, or explicit request
   relationship, with a tolerance. With no mockup, use repository anchors and proceed. Skip for
   appearance-only and non-visual tasks.
2. PLAN     — Phase 2 -> plan.md. For layout work, derive all style metrics from visual.md.
3. ASSESS   — Phase 3.
4. IMPLEMENT— Phase 4, sequentially, one leaf worker per plan phase in NESTED mode. Give layout
   workers visual.md and require exact contract compliance. Implementation and later impl-fix
   workers edit and report; they do NOT commit. You own every commit boundary.
5. BUILD    — Phase 5, using the resolved BUILD. Proactively stop only a straggler whose executable
   path exactly equals EXE before building. If the build itself reports C1041, LNK1104, a locked
   output, access denied, or file in use, AGENTS.md wins: do not retry or use a workaround. Return a
   global hard-stop asking the user to close this checkout's app/debugger.
6. REVIEW   — Phase 6 but a SINGLE pass: one 6a, then one 6b if NEEDS_CHANGES, followed by a build.
   Give the reviewer visual.md for layout work so contract violations are review findings.
6b. NORMALIZE — on native Windows only, run task-think Phase 7 on the exact task-owned source/config
   paths after the last review edit and before the implementation commit. Then run one final BUILD
   so the bytes about to be committed are the bytes verified. For every later impl-fix attempt,
   normalize its exact touched paths before its final build and commit. On WSL/Linux/macOS keep
   project and `.ai` text LF/no-BOM and never run CRLF normalization.
7. COMMIT   — verify every dirty path belongs to this task and matches the owned write sets. Stage
   only explicit task-owned paths; `git add -A` is forbidden. Commit an intended submodule only when
   its own preflight was clean and its changes belong to this task, then stage that pointer. Use a
   concise plain-language subject (about 50-60 characters, matching recent history), a short body
   only when necessary, and no `Autotask:`, attempt, `Co-Authored-By:`, or assistant attribution.
   Record the commit as IMPL_SHA and attempt 1.
8. TEST     — run `.agents/shared/test-loop.md` to APPROVED / BLOCKED / attempt cap and read its
   Codex-only `references/computer-use-testing.md` adapter. Spawn a leaf test-author and feed it BOTH
   sides per test-loop.md "Design the tests from THIS task":
   (1) the TASK SPEC — this task's full description block including `Design-Basis:` PLUS referenced
   IMAGES when present (read them), and (2) the implementation — `git show
   <IMPL_SHA>` + touched files. It designs a falsifiable oracle per change and writes the plan into
   `<TASK_DIR>/test.md` BEFORE running. Give it `COMPUTER_USE_POLICY` and the adapter path; it selects
   `Driver: overlay` or `Driver: hybrid` per check and predeclares its action, fallback, target, ready
   marker, and safety envelope. Only the task-runner operates Computer Use. Visual checks compare
   old/new art when it exists; otherwise use exact task criteria, visual.md, current/legacy analogues,
   style-token identity, and TASK_BASE_SHA behavior. Never synthesize target art. For layout, require
   arithmetic geometry checks, a same-scale best-reference/baseline comparison, and an adversarial
   contract pass. Cover every `Observable:` and named surface; never reuse a generic test.
   Treat TASK_BASE_SHA, not `IMPL_SHA^`, as the pre-task OLD baseline across every fix attempt. Drive
   RUN/ASSESS adversarially: missing evidence = TEST_FLAW; no difference from TASK_BASE_SHA =
   IMPL_BUG. If attempt equals MAX_ATTEMPTS, block before creating another impl-fix commit. Otherwise
   a leaf impl-fix edits without committing; you normalize if native Windows, build, stage exact
   owned paths, commit the next attempt, and update IMPL_SHA.
   Codex safety overrides for the shared mechanics:
   - Overlay code may modify tracked task-owned files only; it may not create untracked source files.
     Inventory its paths in `<TASK_DIR>/test-overlay.paths`.
   - Save it with `git diff --binary HEAD > <TASK_DIR>/test-overlay.patch`, which captures staged
     changes after `git apply --3way`. Verify the patch is nonempty and reappliable. Restore only the
     inventoried overlay paths to IMPL_SHA (including inside an intended submodule) rather than
     resetting the whole checkout. If any unexpected path is dirty, hard-stop without resetting it.
   - Use `TelegramForcePortable/.codex-implement-test-copy` as the ownership marker. SETUP may delete
     only marked live; otherwise move unmarked live to real when real is absent, or stop if both
     exist. Copy golden to live and mark it. At terminal cleanup delete marked live, then, only if
     real exists, MOVE it back to live so later manual changes are recaptured.
   - Set `RUN_DIR=<TASK_DIR>/runs/attempt-<n>/run-<m>/` and `EVIDENCE_DIR=RUN_DIR`; overlay and
     assessor use it for logs, screenshots, `app_stdout.txt`, and `app_stderr.txt`. Remove stale live-copy
     `tdata/working`, record a dump baseline, and stop with BLOCKED(test) at MAX_TEST_RUNS even when
     TEST_FLAW repairs did not consume an implementation attempt.
   - Repository file-lock instructions remain authoritative: clean up the exact EXE proactively,
     but never retry after an actual lock build failure.

Skip TEST only for documentation or metadata with no runnable behavior; record
`VERDICT: NOT_APPLICABLE` and the file-level validation. "Config" alone is not a reason to skip.
If you must return `STATUS: BLOCKED`, FIRST leave the checkout clean and buildable for the next
task by restoring only proven task-owned paths to the last green IMPL_SHA, or TASK_BASE_SHA if no
green implementation exists. Never reset an unexpected path. State the blocker type:
`BLOCKED(test)` = a building implementation is committed and only exact named verification remains;
`BLOCKED(impl)` = no green implementation, with HEAD left at TASK_BASE_SHA. A known implementation
bug at the attempt cap is BLOCKED(impl); do not keep a behavior-known-bad commit as successful work.
Reserve unrecoverable for a checkout you cannot safely return to a clean, buildable commit.
On APPROVED or justified NOT_APPLICABLE, promote `<TASK_DIR>/about.proposed.md` to the project
blueprint before replying. On BLOCKED, keep the prior blueprint and retain the proposal only as a
task artifact.
Before replying, write `<TASK_DIR>/result.md` with these exact fields:
```
# Task result: <TASK_ID>
STATUS: DONE | BLOCKED
Verdict: APPROVED | NOT_APPLICABLE | <specific blocker>
Blocker-Type: none | test | impl | unrecoverable
Task-Base-SHA: <sha>
Implementation-SHA: <sha or none>
Attempts: <n>
Test-Runs: <n>
UI-Driver: overlay | hybrid | mixed | hybrid-unavailable | not-applicable
Touched: <repo paths or none>
Test-Report: <path or not-applicable>
Evidence: <specific log/screenshot paths and what they prove>
Unverified: none | <exact behavior and manual follow-up>
Checkout: clean-buildable | unsafe
Discovered: none | present

## Discovered tasks
<ordered complete `### discovered-N: ...` blocks; dependencies may name existing ids or earlier placeholders; omit when none>
```

`STATUS: DONE` requires APPROVED or a justified NOT_APPLICABLE verdict, a clean checkout, and all
task-owned changes committed after any native-Windows normalization. The result file is mandatory.

Reply with only the compact summary block from test-loop.md
(TASK/STATUS/VERDICT/ATTEMPTS/TOUCHED/DISCOVERED/NOTES); include result.md and test.md paths plus the
key evidence or exact unverified behavior in NOTES.
````

## Completion

When the loop ends (every task is `approved` or `blocked`):
1. **FIRST — LOUDLY AND IN BOLD — list everything that did NOT fully succeed.** Every `blocked`
   task and every task whose tests could not fully verify it gets its own bold line stating EXACTLY
   what failed or what is still UNVERIFIED and the manual follow-up needed, e.g.
   **"⚠️ <letter> — impl committed (<sha>) & review-approved, but <behavior> is UNVERIFIED
   (<why, e.g. test-harness limit>); verify manually — <test.md/result.md path>"**. Make this block
   impossible to miss. If everything passed and was verified, say that explicitly instead.
2. Summarize each validated result.md: approved vs blocked, implementation SHA, attempts, files
   touched, and the exact log/screenshot evidence or unverified behavior.
3. List any discovered tasks that were added.
4. Note the project name for `implement <project> <follow-up>`.
5. Show total elapsed time (`Xh Ym Zs`, omit zero components).
6. Remind that test overlays are saved as `.ai/<project>/<letter>/test-overlay.patch`; the working
   tree is clean at the final retained implementation HEAD, and overlays are not present in it.
7. In Goal mode, mark complete only if every task is approved. With any blocked task, report the
   terminal pipeline state without claiming the objective was achieved.

## Error handling

- Follow task-think's retry ladder only for disposable leaf phases. Never automatically duplicate a
  stateful task-runner. A runner returning BLOCKED does not stop the loop by default; record it and
  continue while the checkout stays clean and buildable. Stop only when continuing is impossible.
- Retry a malformed plan with its disposable planner; repair runner artifacts in the same runner's follow-up turn, never a duplicate.
- On a file-lock build error, follow AGENTS.md: stop immediately, do not retry or attempt a
  workaround, and ask the user to close this checkout's app and debugger. Proactive pre-build
  cleanup remains full-path-scoped to EXE; image-name-wide termination is always forbidden.
- The launch gate (Phase A) guarantees the test account exists before any work begins.
- Never stash, stage, commit, restore, or reset unrelated user changes. Unexpected dirty paths are a
  hard stop, not permission to clean the checkout.
- Keep `.ai/` artifacts and project text LF/no-BOM on WSL; normalize CRLF/no-BOM only on native Windows.

## User invocation

`Use local implement skill: <request or path>`; resume/extend with `<project> [additional change]`. A prepared `.ai/<project>/tasks/about.md` is the automatic project-only source.
