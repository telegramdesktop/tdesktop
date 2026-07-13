---
description: Autonomously implement a task (split into a task list if needed), then implement + test each task to approval via isolated per-task subagents
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Task, AskUserQuestion, TodoWrite
---

# Implement - Autonomous Implement-and-Test Orchestrator

You are the **top orchestrator**. You take a request — an inline description OR a task-list file —
normalize it into a project with a testability-split task list, and drive each task to
test-approval through an isolated per-task `task-runner` subagent. Your context must stay lean: you
hold only the task list and one compact summary per task. All heavy work (planning, coding,
building, testing) happens inside subagents whose context is discarded.

This is the tested superset of `/task`: it reuses `/task`'s phase prompts for implementation and
adds the impl⇄test loop defined in `.agents/shared/test-loop.md`.

Mockups, screenshots, and graphic references are optional evidence, not launch gates. Their absence
alone must never stop or block the run. For visual work, use explicit task facts first, supplied
references when present, then current/legacy repository UI and style tokens, and finally the closest
established desktop convention plus the smallest common-sense change. Only an expressly required
exact artifact that is unavailable and unrecoverable is missing required input.

**Arguments:** `$ARGUMENTS` = ONE of:
- an inline task description (e.g. `add a dark-mode toggle to settings`)
- a path to a task-list file (e.g. `.ai/communities/tasks.txt` — a rough list of tasks to refine)
- an existing project name to resume (e.g. `communities`), optionally followed by extra work
- **just a project name with a prepared `.ai/<project>/tasks/about.md`** — the default task source
  (see Artifacts). With no other input, `/implement <project>` plans and implements straight from
  that file, so `/implement communities` alone fires the full pipeline off `.ai/communities/tasks/about.md`.
May also reference attached images.

## Config

Runs in the **current checkout** — wherever `/implement` is invoked. No worktrees are created; all
paths below are relative to that repository root.

```
BUILD         = cmake --build ./out --config Debug --target Telegram
EXE           = ./out/Debug/Telegram.exe
TEST_ACCOUNT  = ./out/Debug/test_TelegramForcePortable   # user-prepared golden; launch gate aborts if absent
MAX_ATTEMPTS  = 4
```

The test binary is **always launched with `-testagent`** (see test-loop.md "Crashes & assertions"):
it suppresses the Debug Abort/Retry/Ignore dialogs that would hang the run, turns any CRT/STL
assertion (and a frozen main thread) into an immediate crash with a written `tdata/working` report,
and writes the assertion text to a captured stderr file so a crash is diagnosable instead of a silent
hang. Key crash detection on the report file, not the exit code.

Tasks run **sequentially** in this one checkout (the build cache stays warm; app runs must
serialize against the account anyway). To parallelize, launch `/implement` in a different
checkout/slot (e.g. `C:\Telegram\tdesktop`, `D:\Telegram\tdesktop`, `D:\Telegram\twin`) — each run
is independent and single-tree. Don't run the **test phase** in two slots against the same account
at once (concurrent clients on one auth key can trigger a session reset); give parallel slots
separate test accounts.

## Artifacts (per project)

- `.ai/<project>/tasks/about.md` — the **default task source**: a human-prepared description (or
  rough list) of the batch to implement, with any available mockups optionally dropped beside it.
  This is the file you prepare; the planner reads it as SOURCE when `/implement <project>` is invoked
  with no other input. It is **distinct** from the project blueprint `.ai/<project>/about.md` (the
  `tasks/` subdir is what disambiguates them).
- `.ai/<project>/implementing.md` — the canonical, final, testability-split task list (descriptions
  + status). Your single source of truth; you are its only writer.
- `.ai/<project>/images/` — illustrations referenced by tasks (`images/01.png`, ...).
- `.ai/<project>/<letter>/` — per-task artifacts (context, plan, review, test, result, overlay).
- `.ai/<project>/about.md` — project blueprint (the `/task` convention).

## Done (for `/goal` loop mode)

The run is **done** when every task in `implementing.md` has `Status: approved` or
`Status: blocked: <reason>`. Under a `/goal` loop this is the stop condition. The run is
**resumable**: re-invoking with the project name reads `implementing.md` and continues from the
first unfinished task.

## Phase A: Setup & input resolution

1. Record start time (`Get-Date`).
2. **Test-account gate (hard precondition — before any work).** If
   `out/Debug/test_TelegramForcePortable` does NOT exist, STOP the entire command immediately and
   tell the user: the test account is not prepared — create `out/Debug/test_TelegramForcePortable`
   (a portable-data folder authed to a throwaway test account) before `/implement` can run, because
   autonomous testing is impossible without it. Do no implementation work.
3. **Resolve `$ARGUMENTS` into (project, SOURCE, mode) — without reading task files or images.**
   The main thread never loads task prose or assets; resolving needs only paths and existence
   checks. SOURCE ends up as EITHER inline text OR a confirmed file path (the planner reads it).
   - **File input** — if the first token is a path: confirm it exists (`Test-Path`, do NOT read it)
     and set SOURCE = that path. If the path is under `.ai/<name>/`, project = `<name>`; else derive
     a short kebab name from the filename. Mode = **extend** if that project already has
     `implementing.md`, else new.
   - **Existing project** — else if `.ai/<FIRST_TOKEN>/` exists: project = `FIRST_TOKEN`.
     - If there is a **remainder** → mode = **extend**: if the remainder is itself a path to an
       existing file, SOURCE = that path (confirm with `Test-Path`, do NOT read it); otherwise
       SOURCE = the remainder text.
     - If the remainder is **empty**, resolve SOURCE in this priority order (existence checks only,
       `Test-Path`, do NOT read):
       1. If `.ai/<project>/tasks/about.md` exists → SOURCE = that file (the **default task
          source**); mode = **extend** if `implementing.md` already exists, else **new**. This is
          the `/implement <project>` with a prepared task source path — it fires the full pipeline.
       2. Else if `implementing.md` exists → mode = **resume** (no SOURCE; Phase C finishes the
          still-unfinished tasks).
       3. Else there is nothing to implement — tell the user to prepare `.ai/<project>/tasks/about.md`
          (or pass a description / task-file path) and stop.
   - **New inline** — else SOURCE = the `$ARGUMENTS` text; pick a unique short kebab-case project
     name (consult `ls .ai/`).
   After this step you always have a project name and either a SOURCE (inline text or a confirmed
   path) or mode = **resume** — and you have read neither the file nor any image.
4. Create `.ai/<project>/` and `.ai/<project>/images/` if new.
5. **Persist supplied images when present.** The planner reads images as files, and subagents cannot
   see chat attachments. The main thread **cannot** save a pasted/inline chat image to disk
   (`Write` is text-only; there is no save-attachment tool, and on Windows clipboard-paste isn't even
   supported). For a chat-only image, pass a detailed textual description; ask for an on-disk copy
   only when exact unavailable bytes are themselves required. With no image, continue normally. Do
   not claim to have saved one. SOURCE-referenced images are the planner's job.
6. If mode = **resume**, skip Phase B and go to Phase C.

## Phase B: Planning & testability split

Spawn one planner subagent (Task, `general-purpose`):

```
You are a planning/splitting agent for a large C++ codebase (Telegram Desktop).

SOURCE — EITHER an inline request OR a path to a task-list file. If it is a PATH, READ it yourself
(and any task files it points to); the main thread has NOT read it. If it is inline text, use it as
the request:
<the inline description, or the file path>
PROJECT: <project>     MODE: <new | extend>

IMAGES (optional) — the SOURCE and/or its task file may reference images by path (resolve them relative to the
SOURCE file's directory, or use absolute paths; when SOURCE is `.ai/<project>/tasks/about.md`, its
sibling files in `.ai/<project>/tasks/` — e.g. the mockup PNGs there — are those images). READ every
referenced image yourself, then COPY
each into `.ai/<project>/images/` with a descriptive kebab-case name, and reference it from the
specific task(s) it pertains to (see "Images per task" below). The main thread did NOT read or move
these — that is your job. If an image exists only as a textual description (because the user pasted
it into chat and it could not be saved to a file), it is provided here — treat that description as
visual evidence: <description(s) or none>. If none exists, continue from the request and repository;
do not ask for mockups or weaken, omit, or block a visual task solely for lacking optional
references. The express exact-artifact exception above still applies.

Read AGENTS.md. Briefly scan the codebase to gauge scope. Produce the FINAL ordered task list that
satisfies BOTH constraints for every task:

- **Implementable in one pass**: a single agent with a ~200k-token budget must be able to implement
  the task fully on its own WITHOUT triggering context compaction — i.e. a bounded change it can
  read and edit across a handful of files, not a sweep across dozens. If a unit is too big, split
  it.
- **Independently testable**: each task must yield an observable behavior the test agent can drive
  from an in-app debug overlay and verify via log/screenshot. Split on testable seams, so each task
  ends at a point where something concrete can be exercised and checked.

Use the minimal number of tasks subject to both constraints; preserve dependency order (a task
comes before any task that depends on it). If the SOURCE is already a list, respect its intended
breakdown and refine only as needed: split entries that are too big or not independently testable;
you may merge trivially tiny adjacent entries if the result is still one testable unit.

Write `.ai/<project>/implementing.md` in EXACTLY this format:

# Implementing: <project>

## Goal
<one-line overall goal>

## Tasks

### a: <imperative title>
Status: todo
<2-4 line self-contained description: what to implement and the observable, testable result. Enough
that a fresh agent can act on it.>
Visual: layout | appearance       (user-visible visual/asset changes only; omit otherwise)
Design-Basis: <ordered request/image/current/legacy/repository evidence and assumptions; visual tasks only>
Images: images/<file> — <caption>      (this line only if the task uses an image)

### b: <imperative title>
Status: todo
<...>

**Images per task (required when supplied).** Attach every pertinent supplied image via `Images:`,
with a precise caption. Explicitly account for irrelevant/context-only images without attaching or
treating them as targets. With none on a visual task, omit `Images:`, cite non-image evidence in
`Design-Basis:`, and never create a placeholder. Non-visual tasks omit both.

**Visual classification (required for visual/asset changes).** For every task that changes
how user-visible UI, rendered output, or an asset looks, add `Visual:`; it routes the task-runner:
- `Visual: layout` — changes element sizes, proportions, spacing/margins, alignment, or component
  geometry. It triggers a numeric design contract and geometry-MEASURING oracle whether or not a
  mockup exists.
- `Visual: appearance` — must match COLORS / wording / which-style / glyph identity, but NOT
  proportions or geometry (e.g. "make Decline red", "use the box-button palette"). Lighter check; no
  contract.
- omit the line — the task changes no appearance.
Classify from the requested change, never from image availability. When torn between the two, choose
`layout` for anything built from multiple sized/positioned pieces. Every visual task must include
`Design-Basis:`; images are only one possible basis.

Use spreadsheet-style task ids (`a`...`z`, `aa`...). Do not plan internals or implement. When done, reply with ONLY a
compact confirmation — `ready — <N> tasks` (extend: `ready — appended <letters>`); do NOT echo the
task list or image contents back, the main thread reads `implementing.md` itself.
```

For **extend** mode, instead instruct the planner to FIRST read the existing `implementing.md`, then
rewrite it as: (1) a TRIMMED completed-history — keep only the **three most recent** `Status: approved`
task blocks (the three nearest the bottom of the file) and drop all earlier approved ones; (2) every
still-unfinished task left untouched, in place and with its status — that is all `todo`, `in-progress`,
and `blocked` blocks (never drop these); then (3) APPEND new tasks starting after the highest id in
the pre-trim union of headings and `.ai/<project>/<id>/` artifact directories. Never reuse an id
merely because trimming removed its heading. The trim only removes
already-approved entries from the list — it never touches the per-task `.ai/<project>/<letter>/`
artifacts on disk, so a follow-up letter can still read an earlier letter's `context.md` even after its
block was trimmed out of `implementing.md`. It must append ONLY tasks from SOURCE not already
represented in `implementing.md` — so re-running `/implement <project>` against an unchanged default
`tasks/about.md` appends nothing (the planner replies `ready — appended (none)`, still applying the
completed-history trim) and Phase C just finishes whatever is still unfinished. (Any
`todo`/`in-progress` leftovers from an interrupted run are picked up by Phase C regardless, so
defaulting to extend never loses an in-flight batch — it is a superset of resume.)

After the planner replies `ready`, read `implementing.md` back ONCE (your first and only load of the
task prose; you never read the images). Initialize a TodoWrite list mirroring the tasks so progress
is visible.

## Phase C: Per-task loop

For each task in `implementing.md` whose `Status` is not `approved`/`blocked`, in order:

1. Record `TASK_BASE_SHA = HEAD`, then set `Status: in-progress` (and mark TodoWrite in progress).
2. Spawn ONE `task-runner` subagent (Task, `general-purpose`) with the prompt below. Wait for it.
3. Read ONLY its compact summary block (the `task-runner` writes all detail to `.ai/`).
4. Update the task's `Status:` line — `approved` if `STATUS: DONE`, else `blocked: <reason>`.
5. If `DISCOVERED` lists follow-ups, send them through a planner to produce complete ID-free task
   blocks. Rescan the union of headings and artifact dirs, assign unused spreadsheet ids, validate
   dependencies, append after current tasks, and add them to TodoWrite. Only the main thread assigns
   ids and writes `implementing.md`.
6. If `STATUS: BLOCKED`, **do NOT stop the loop — prioritize continuing development.** This often
   runs unattended for hours, so NEVER pause to ask the user whether to go on; record the blocker
   and move to the next task as long as further progress is possible. Distinguish:
   - **Test-blocked** — the runner committed a building impl and only its in-app verification could
     not complete (a test-harness limitation, or the attempt cap hit on a test flaw rather than a
     real bug). The committed code is on disk, so CONTINUE. Capture EXACTLY what was left unverified
     for the loud final report.
   - **Impl-blocked but checkout clean** — no green impl for THIS task, but the runner left HEAD at
     a prior committed, buildable commit. CONTINUE — later tasks may be independent of this one;
     record that this task's behavior is missing.
   - **Hard stop ONLY when continuing is truly impossible** — the checkout is left broken /
     uncommitted / non-buildable (a later task-runner could not even start from a clean base), or a
     global environment failure blocks all work (file-lock build error needing the user to close
     `Telegram.exe`; the test-account gate). Only then stop and report.
   Before spawning the next task, confirm the working tree is clean and at a buildable commit
   (`git status` + the runner's summary). If a blocked runner left it dirty or broken, reset to the
   last known-good commit first; if you cannot recover a clean buildable base, that is the hard-stop
   case above. Every blocked/unverified task MUST be surfaced LOUDLY in the Completion report —
   continuing is never the same as silently passing.

### task-runner prompt

```
You are a task-runner for ONE task in an autonomous implement-and-test workflow on Telegram
Desktop (C++ / Qt). You own this task end to end and isolate its context from the orchestrator.
You MAY and SHOULD spawn your own subagents (the Task tool is available to you).

PROJECT: <project>     TASK: <letter> — <title>
TASK DESCRIPTION:
<the task's full description block from implementing.md>
IMAGES: <referenced .ai/<project>/images/* paths, or none — Read them if present>
TASK_DIR: .ai/<project>/<letter>/
TASK_ID: <project>-<letter>
TASK_BASE_SHA: <HEAD before this runner was spawned>

Config (paths relative to this checkout): BUILD=<...> EXE=<...> MAX_ATTEMPTS=<...>. The test account
is the out/Debug/ portable-data folders (see test-loop.md "Test account"). For each test execution,
set `EVIDENCE_DIR=<TASK_DIR>/runs/attempt-<n>/run-<m>/` and create it before launch.

Read first: AGENTS.md; REVIEW.md; `.claude/commands/task.md` (for the exact Phase 1-6 prompt
templates); `.agents/shared/test-loop.md` (for the testing phase). For a follow-up letter, also read
`.ai/<project>/about.md` and the previous letter's `context.md`.

Treat `IMAGES: none` as normal. Missing mockups alone never justify pausing, blocking, or asking the
user. For visual work use `Design-Basis:`; otherwise use the task description and repository.

Run this pipeline for THIS task only, spawning a fresh subagent per phase (so each phase's output
stays in YOUR context, not the orchestrator's):

1. CONTEXT  — run task.md's Phase 1 (new) or Phase 1F (follow-up) prompt for this task; produces
   `<TASK_DIR>/context.md` (and `about.md` for the project).
1b. DESIGN-SPEC — only if the task is `Visual: layout`. Inventory `Design-Basis:`: read supplied
   images when present, then inspect current/legacy implementations and the closest desktop
   widgets/style tokens. Write `<TASK_DIR>/visual.md` with cited evidence, assumptions, and the
   ordered derivation from `.agents/shared/test-loop.md`; every quantity must use a font metric,
   style token, sibling geometry, or explicit task relationship and have a tolerance. With no
   mockup, use repository anchors and proceed. Before PLAN, verify `visual.md` cites available design
   sources, records assumptions, and contains desktop anchors, an ordered derivation, tolerances,
   and falsifiable geometry checks. Skip for non-visual and appearance-only tasks.
2. PLAN     — task.md Phase 2 -> `<TASK_DIR>/plan.md`. For a `Visual: layout` task the plan's `.style`
   metrics come straight from `<TASK_DIR>/visual.md`.
3. ASSESS   — task.md Phase 3 (refine plan, size phases).
4. IMPLEMENT— task.md Phase 4, one subagent per plan phase. For a `Visual: layout` task, give each
   impl subagent `<TASK_DIR>/visual.md` and require its `.style` metrics to satisfy that contract
   exactly (no eyeballed sizes). Implementation agents do NOT commit yet; you commit after build
   passes.
5. BUILD    — task.md Phase 5 (build with BUILD, fix errors). Proactive cleanup may stop only THIS
   checkout's full-path binary before building. If the build reports a file-lock error, stop
   immediately without retry/workaround and ask the user to close the app/debugger.
6. REVIEW   — task.md Phase 6 but a SINGLE pass (not 3): one review agent, then one fix agent if
   NEEDS_CHANGES, then rebuild. (Tests catch behavior; review catches dead code / duplication /
   placement / style.) For a `Visual: layout` task, also hand the review agent `<TASK_DIR>/visual.md`
   so it flags any `.style` metric that violates the contract.
7. COMMIT   — `git add -A && git commit` with a concise plain-language subject (≤ ~50-60 chars,
   matching recent `git log` style; usually the whole message — add a short plain body only if the
   subject can't carry it). NO `Autotask:`/attempt trailer and NO `Co-Authored-By:`/attribution line
   (this overrides the default; see test-loop.md "Commit message"). Commit submodules first if dirty,
   then bump the pointer. Record the commit SHA as IMPL_SHA (you track the attempt number yourself).
8. TEST     — run the loop in `.agents/shared/test-loop.md` to APPROVED, BLOCKED, or attempt cap.
   Spawn a test-author subagent and feed it BOTH sides per test-loop.md "Design the tests from THIS
   task": (1) the TASK SPEC — this task's full description including `Design-Basis:` PLUS referenced
   IMAGES when present, and (2) the implementation —
   `git show <IMPL_SHA>` + touched files. It designs a falsifiable oracle per change and writes the
   plan into `<TASK_DIR>/test.md` BEFORE running (visual checks compare old/new art when it exists;
   otherwise they use exact task criteria, the numeric contract, current/legacy analogues, style
   token/resource identity, and pre-task behavior; never synthesize target art. For a
   `Visual: layout` task, measured sizes/spacings/alignment must satisfy each derivation line within
   tolerance, using a same-scale best-reference/baseline comparison plus an adversarial contract pass;
   "all elements present" is NOT a pass — see test-loop.md "Visual contract"), covers every surface
   the task names, and never reuses another task's
   navigate+screenshot. You drive RUN/ASSESS yourself, ADVERSARIALLY (no pass-by-inference; missing
   evidence = TEST_FLAW; no-difference-from-before = IMPL_BUG), treating TASK_BASE_SHA rather than
   `IMPL_SHA^` as the baseline across every fix attempt, and keep the human-readable
   `<TASK_DIR>/test.md` report. Spawn an impl-fix subagent on IMPL_BUG (it commits the next attempt →
   new IMPL_SHA). After each run, save the overlay patch into TASK_DIR and `git reset --hard
   <IMPL_SHA>` so the checkout returns to impl-only. Run the test-account SETUP steps before each
   launch, set the run-specific `EVIDENCE_DIR`, and honor every test-account hard rule (serialize
   app runs; avoid destructive calls).

Skip TEST only if the task changed no runnable behavior (docs/config only) — say so explicitly.

If you must return `STATUS: BLOCKED`, FIRST leave the checkout clean and buildable for the next
task: `git reset --hard` to your last green IMPL_SHA if you have one, else to TASK_BASE_SHA
(never leave uncommitted or non-building changes behind). In the summary state the blocker TYPE so
the orchestrator can continue: `BLOCKED(test)` = impl committed & building, only verification
incomplete (give the exact unverified behavior + the commit SHA); `BLOCKED(impl)` = no green impl
for this task (say whether HEAD is left clean/buildable at a prior commit). Reserve a true
unrecoverable stop for a broken checkout you cannot reset to a buildable commit.

When done, write nothing new to chat except the compact summary block from test-loop.md
("TASK/STATUS/VERDICT/ATTEMPTS/TOUCHED/DISCOVERED/NOTES"). This wrapper has no `result.md`, so put
concise semicolon-separated follow-ups inline in `DISCOVERED`, or `none`. All reasoning lives in `.ai/`.
```

## Completion

When the loop ends (every task is `approved` or `blocked`):
1. **FIRST — LOUDLY AND IN BOLD — list everything that did NOT fully succeed.** Every `blocked`
   task and every task whose tests could not fully verify it gets its own bold line stating EXACTLY
   what failed or what is still UNVERIFIED and the manual follow-up needed, e.g.
   **"⚠️ <letter> — impl committed (<sha>) & review-approved, but <behavior> is UNVERIFIED
   (<why, e.g. test-harness limit>); verify manually"**. Make this block impossible to miss. If
   everything passed AND verified, say that explicitly instead.
2. Summarize per task: approved vs blocked, attempts, files touched, key test evidence.
3. List any discovered tasks that were added.
4. Note the project name for `/implement <project> <follow-up>`.
5. Show total elapsed time (`Xh Ym Zs`, omit zero components).
6. Remind that test overlays are saved as `.ai/<project>/<letter>/test-overlay.patch` and the
   checkout is left at each task's implementation commit (overlays reset away).

## Error handling

- A `task-runner` returning BLOCKED does NOT stop the loop by default — record the blocker and
  continue to the next task as long as the checkout stays clean and buildable (see Phase C step 6).
  Stop the loop ONLY when continuing is impossible: a broken/non-buildable checkout, or a global
  environment failure (file lock requiring user action, missing test account). Whatever the outcome, report
  every blocker's reason and `test.md` path LOUDLY in the Completion summary.
- If `implementing.md` or any artifact is malformed, re-spawn that step with tighter instructions.
- Never proceed past a file-lock build error — ask the user to close `Telegram.exe`.
- The launch gate (Phase A) guarantees the test account exists before any work begins; if it is
  absent the command never starts.
- Missing optional screenshots, mockups, or graphics are never an error or blocker.
