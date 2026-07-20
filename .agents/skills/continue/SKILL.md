---
name: continue
description: Continue autonomous Telegram Desktop development from the shared ai-tdesktop repository. Use when the user invokes $continue or /continue, asks Codex to keep working through the AI queue, or wants one command to process the local inbox, resume this checkout's unfinished work, and start ready shared tasks until nothing eligible remains.
---

# Continue AI Work

Act as the checkout-level scheduler. Keep looping until the inbox is empty and
no eligible work remains. Delegate inbox planning and one-task execution; do not
plan or implement Telegram changes in this scheduler session.

This is the default development command and the successor to the old `task` and
`implement` workflows. Inbox processing owns request splitting and project
routing; `perform-task` owns context, planning, implementation, review, Debug
build, test-loop, evidence, and final publication.

## Resolve the workspace

Run from a Telegram Desktop checkout. Read `AGENTS.md`, then use the shared
helper with the host's Python 3 command:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py queue
```

Use `python` or `py -3` when appropriate. Save `checkout_tag`, `ai_main`,
`slot_worktree`, and `source_root` from its JSON. Read `ai_main/AGENTS.md`.

Stop before mutating anything when `violations` is nonempty or AI master is
dirty. Dirty task-scoped AI files are the active checkout's local resumable
phase state and are expected; resume from them, never discard them. Other
uncommitted slot changes under `tasks/`, `projects/`, or `receipts/` are
disposable leftovers of an interrupted worker; the ignored inbox snapshot and
published task results retain every durable input needed to redo them.
Restore those exact tracked paths to the slot branch head, delete their
untracked files, and continue. Stop instead of cleaning when any other slot
path changed, and never clean the main worktree or stash anywhere.
Unpublished clean AI slot commits are incomplete publication; run the
helper's `publish` command before selecting work.

The canonical lifecycle is deliberately small:

- `todo` with `claimed_by: null` is ready shared work;
- `in-progress` with this `checkout_tag` is this checkout's one active task;
- `blocked` with this tag is a rare published unfinished boundary;
- `approved` is the only completed terminal state.

`Start` atomically assigns an unclaimed task and changes it to `in-progress`.
Normal phase artifacts remain local and uncommitted in the slot worktree.
`Approve` publishes all final AI artifacts and state in one commit. `Block` is
permitted only for a genuine exhausted implementation or verification blocker,
not for an interrupted agent session. Never publish `Claim`, phase checkpoint,
or `Resume` commits. Existing claimed `todo` records from the older workflow
remain startable but do not justify creating new reservations.

Never infer ownership from an inbox receipt. Do not steal work from another
checkout. Moving an unfinished task to another checkout is a rare explicit
human reassignment that may restart the task and discard checkout-local phase
artifacts; it is never automatic scheduler behavior.

## Interpret scope hints

Treat text after `$continue` or `/continue` as optional natural-language
guidance for new shared work. Its own wording decides its strength. A
preference such as "payments tasks first" only reorders selection; the run
still drains every eligible task. A restriction such as "only the payments
tasks" limits new shared work to matching tasks and ends the run when none
remain. Neither form reserves a batch. Start only one shared task at a time,
finish or exceptionally block it, refresh canonical state, then choose again.

Always resume this checkout's `in-progress` task and previously blocked work
before applying a priority hint to new shared work, unless the user expressly
asks to stop or reassign it.

## Main loop

Create an empty invocation-local `attempted_blocked` set, then repeat. Refresh
queue JSON after every delegated operation and state transition.

Before publishing any new canonical `Start` commit, require a startable
environment: a clean Telegram source checkout with clean submodules and no
unrelated untracked files, plus an existing
`out/Debug/test_TelegramForcePortable` golden account. A failed check is a
global hard stop before claiming; never reserve shared work this checkout
cannot immediately run. Resuming and retrying already-owned work keeps the
performer's own preflight rules instead.

### 1. Resume active work

If this checkout has an `in-progress` task, select it and spawn one performer.
Its task-scoped dirty AI artifacts are the resumption handoff. There must be at
most one active task. Resume it before inbox processing because the inbox worker
requires a clean slot; after the task finishes, the next loop iteration handles
the inbox.

### 2. Process the inbox

When no task is active and `inbox_nonempty` is true, spawn one inbox worker
with `fork_turns: "none"`. Give it the source checkout path and instruct it to
read and use `.agents/skills/process-inbox/SKILL.md` completely. It owns exactly
one inbox transaction, may use the bounded planner delegation required by that
skill, must not implement tasks, and must return the receipt and created ids.

Wait in intervals no longer than 60 seconds. A timeout is not failure. Inspect
the saved target after every wake and validate the receipt plus refreshed queue
before proceeding. Never launch a second worker for the same transaction. If
it cannot publish durable AI state, stop with the inbox transaction recoverable.

### 3. Retry rare blocked work

Otherwise select the first ready task in `own_blocked` whose id is not in
`attempted_blocked`. Readiness means every dependency is `approved`. Add its id
to the set, then reopen it locally:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py retry \
  --task <YYYY/MM/DD/slug>
```

This preserves its ownership, source recovery refs, plans, reviews, tests,
result, and evidence while changing the slot worktree back to local
`in-progress`. It publishes no `Resume` commit. Spawn its performer at the
first incomplete validated boundary.

If it blocks again, leave the new canonical `Block` boundary and do not retry
it again in this invocation. Independent work may continue; the next
invocation gets a fresh retry set.

### 4. Start older reserved work

Otherwise select the first ready legacy `todo` task already owned by this
checkout and start it:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py start \
  --task <YYYY/MM/DD/slug>
```

The resulting canonical `Start` commit changes it to `in-progress`. Leave
legacy reservations with unfinished dependencies untouched and consider later
ready work.

### 5. Start shared work

Otherwise select the first ready unclaimed `todo` task the hint prefers. Under
a restrictive hint consider only matching tasks. With no hint, or when a mere
preference has no matching ready task left, select the first ready task in
normal queue order. Start it with the same helper command. `start` atomically
assigns and activates the task, then publishes its canonical `Start` commit
before source work begins.

A concurrent start may mean another checkout won the task. Never overwrite
shared state; refresh and choose again.

### 6. Stop normally

Stop when the inbox is empty and none of these exist:

- this checkout's active task;
- a ready blocked task not attempted in this invocation;
- a ready legacy reserved task;
- a ready unclaimed task this invocation may still start: any under no hint or
  a preference, only matching ones under a restrictive hint.

Work owned by another checkout does not keep this run alive. A blocked task
already attempted in this invocation remains visible in the final summary but
does not cause a busy loop.

Immediately before this normal stop, run the housekeeping command once:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py archive-stale
```

It publishes one canonical `Archive <slug>` commit for every project whose
tasks are all approved and whose newest task is older than its threshold,
after rewriting the project's relative links for the deeper path. Skip this
housekeeping on any global hard stop. Only routing new work to an archived
project restores it, through the helper's `unarchive` command; nothing
un-archives on a timer.

## Spawn one performer

Spawn exactly one performer for the selected task with `fork_turns: "none"`
and a unique tool-valid name. Tell it:

```text
Use .agents/skills/perform-task/SKILL.md completely.
Source checkout: <source_root>
AI slot worktree: <slot_worktree>
Checkout tag: <checkout_tag>
Task: <task-id>
Own this task until it is approved, genuinely blocked, or reaches a global
hard stop. You may use the bounded leaf delegation required by the skill. Do
not select or start another task.
```

The performer is stateful. Never duplicate it. Poll at no more than 60-second
intervals, distinguish progress from completion using its task artifacts, and
follow up with the same target if it becomes idle before a valid boundary.

After it returns, require one of:

- source checkout clean at the recorded run tip and task `approved` on
  canonical AI master;
- source checkout clean and task exceptionally `blocked` on canonical master,
  with exact unverified behavior;
- a clearly reported global hard stop, leaving the task `in-progress` and all
  task-scoped local state recoverable for the next invocation.

An interruption or environment stop never becomes a convenience `Block`.
After a genuine `Block`, add the task id to `attempted_blocked` and continue
with independent work. A dirty source checkout, file-lock build failure,
missing test account, unsafe publication conflict, or comparable global safety
failure stops the loop.

The missing `test_TelegramForcePortable` golden account is the only
portable-folder global stop. All live/real folder combinations must be
reconciled by `perform-task` according to the shared test-loop protocol.
Computer Use being unavailable because macOS is locked is never a scheduler
stop; the performer must continue with the in-binary overlay driver and
artifact-based assessment.

## Route discovered follow-ups

After every canonical `Approve` or `Block`, read `work/result.md`. If it says
`Discovered: present` and lacks `work/discovered-routed.md`, route the complete
blocks before selecting more shared work.

Spawn one disposable routing worker with `fork_turns: "none"`. Tell it to read
the routing, splitting, task-path, artifact, validation, and publication rules
in `.agents/skills/process-inbox/SKILL.md`, but not to call `prepare`,
`finalize`, or `abort`. Its immutable input is the result, not the inbox. It
must not edit Telegram source, start tasks, or implement work.

The worker must deduplicate existing tasks, create independently testable
unclaimed `todo` tasks and justified project updates, write a discovery
receipt, and write the source task's routing marker. It stages only those
paths, commits `Route follow-ups from <source-task-id>`, and publishes with the
workspace helper. Retry ordinary concurrent-master races; preserve a semantic
conflict or unavailable-remote slot commit and stop.

## Report

Return one compact summary: inbox receipt if processed, tasks approved,
exceptionally blocked tasks with exact unverified behavior and retry status,
tasks started or left queued, routed discoveries, archived projects, any
discarded interrupted-worker leftovers, elapsed time, and why the loop
stopped. Make
any global hard stop or unsafe state unmistakable. Never include source or AI
commit hashes; task ids are the only durable locators.
