---
name: continue
description: Continue autonomous Telegram Desktop development from the shared ai-tdesktop repository. Use when the user invokes $continue or /continue, asks Codex to keep working through the AI queue, or wants one command to process the local inbox, resume this checkout's active task, consume its claimed queue, and claim new work until nothing eligible remains.
---

# Continue AI Work

Act as the checkout-level scheduler. Keep looping until the inbox is empty and
no eligible work remains. Delegate inbox planning and one-task execution; do not
plan or implement Telegram changes in this scheduler session.

This is the default development command and the successor to the old `task` and
`implement` workflows. Inbox processing owns request splitting and project
routing; `perform-task` owns all mature context, planning, implementation,
review, Debug build, test-loop, evidence, and publication behavior.

## Resolve the workspace

Run from a Telegram Desktop checkout. Read `AGENTS.md`, then use the shared
helper with the host's Python 3 command:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py queue
```

Use `python` or `py -3` when appropriate. Save `checkout_tag`, `ai_main`,
`slot_worktree`, and `source_root` from its JSON. Read `ai_main/AGENTS.md`.

Stop before mutating anything when `violations` is nonempty, AI master is
dirty, or the AI slot has changes outside its one active task. Never clean,
stash, reset, or absorb unrelated changes. Unpublished AI slot commits are
resumable state: retry the helper's `publish` command before selecting new
work.

`status` and `claimed_by` are orthogonal:

- `todo` plus `claimed_by: null` is shared unreserved work;
- `todo` plus this `checkout_tag` is this checkout's reserved queue;
- `in-progress` plus this tag is the one active task;
- `approved` and `blocked` are terminal;
- work claimed by another checkout is invisible to this scheduler.

Do not infer a claim from who processed an inbox receipt. Do not steal or
expire another checkout's claim.

## Interpret scope hints

Treat the invoking request after `$continue` or `/continue` as optional natural
language scheduling guidance, not a required command grammar.

A plain invocation claims one shared task at a time. This minimizes abandoned
reservations and lets parallel checkouts distribute the backlog.

When the user expressly asks to reserve or claim a group, claim the matching
ordered set in one operation. Common scopes include:

- all tasks created from the inbox receipt processed by this invocation;
- all unclaimed tasks for a named project;
- an explicit list of friendly titles or task identifiers.

Preserve receipt order, project index order, or explicit user order when one is
available; otherwise use creation date and task identifier. A batch receives
one `claimed_at` value and ascending `claim_order`. Dependencies do not prevent
reservation, but they do prevent a task from starting.

Scope hints filter new claims only. Always resume this checkout's existing
`in-progress` task and then its already claimed queue before taking more shared
work, unless the user explicitly asks to stop or reassign them.

## Main loop

Repeat these steps. Refresh queue JSON after every delegated operation and
state transition; do not rely on a stale snapshot.

### 1. Process the inbox

When `inbox_nonempty` is true, spawn one inbox worker with `fork_turns: "none"`.
Give it the source checkout path and instruct it to read and use
`.agents/skills/process-inbox/SKILL.md` completely. It owns exactly one inbox
transaction, may use the bounded planner delegation required by that skill,
must not implement tasks, and must return the receipt and created task ids.

Wait in intervals no longer than 60 seconds. A timeout is not failure. Inspect
the saved target after every wake and validate the receipt plus refreshed queue
before proceeding. Never launch a second inbox worker for the same transaction.

If inbox processing cannot publish durable AI state, stop. The inbox skill must
leave the input or active transaction recoverable.

### 2. Resume active work

If this checkout has an `in-progress` task, select it. There must be at most
one. Spawn one stateful performer as described below.

### 3. Start reserved work

Otherwise select the first ready task in this checkout's claimed `todo` queue.
Readiness means every `depends_on` task is `approved`. Transition it atomically:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py start \
  --task <YYYY/MM/DD/slug>
```

Then spawn its performer. Leave claimed tasks with unfinished prerequisites as
`todo` and consider later ready tasks.

### 4. Claim shared work

Otherwise inspect unclaimed `todo` work matching the scope. For a plain
invocation select only the first ready task. For an explicit batch reservation,
reserve every matching task in the chosen order, including later tasks whose
dependencies are not approved yet, and pass one `--task` argument per task:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py claim \
  --task <first-id> \
  --task <second-id>
```

The claim is committed and published before source work starts. Refresh the
queue; then return to step 3. A publish race may mean another checkout won the
task. Do not resolve that by overwriting shared state; refresh and choose again.

### 5. Stop normally

Stop when the inbox is empty and none of these exist:

- this checkout's active task;
- a ready task in this checkout's claimed queue;
- a ready unclaimed task for a plain run, or any unclaimed task matching an
  explicit batch-reservation scope.

Claimed tasks belonging to other checkouts do not keep this run alive.
Claimed tasks waiting on prerequisites remain visible in the final summary but
do not cause a busy loop.

## Spawn one performer

Spawn exactly one performer for the selected task with `fork_turns: "none"` and
a unique tool-valid name. Tell it:

```text
Use .agents/skills/perform-task/SKILL.md completely.
Source checkout: <source_root>
AI slot worktree: <slot_worktree>
Checkout tag: <checkout_tag>
Task: <task-id>
Own this task until it reaches approved or blocked. You may use the bounded
leaf delegation required by the skill. Do not select or claim another task.
```

The performer is stateful. Never duplicate it. Poll at no more than 60-second
intervals, distinguish progress from completion using its task artifacts, and
send a follow-up to the same target if it becomes idle without a terminal state.

After it returns, require:

- source checkout clean and at the performer's retained commit;
- task `state.yaml` terminal and published to AI master; or
- a clearly reported global hard stop that makes further work unsafe.

A clean terminal `blocked` task does not stop the scheduler; continue with
independent work. A dirty checkout, file-lock build failure, missing test
account, unresolved AI publication conflict, or other global environment
failure stops the loop.

The missing `test_TelegramForcePortable` golden account is the only
portable-folder state that is a global stop. Live and real portable folders
coexisting, either one being absent, or any ownership-marker state must be
reconciled by `perform-task` according to the shared test-loop protocol and
must never stop `/continue`.

## Route discovered follow-ups

After every terminal performer, read its published `work/result.md`. If it says
`Discovered: present` and has no `work/discovered-routed.md`, route the complete
blocks under `## Discovered tasks` before selecting more shared work.

Spawn one disposable routing worker with `fork_turns: "none"`. Tell it to read
the routing, splitting, task-path, artifact, validation, and publication rules
in `.agents/skills/process-inbox/SKILL.md`, but not to call `prepare`,
`finalize`, or `abort`: its immutable input is the published result, not the
human inbox. It must not edit Telegram source, claim, or implement work.

The worker must:

1. deduplicate against existing tasks and discovery receipts;
2. preserve each independently testable follow-up, its provenance, and valid
   dependencies;
3. create dated unclaimed `todo` tasks and any justified project/index updates;
4. create `receipts/YYYY/MM/DD/discovered-<source-slug>.md` mapping every block;
5. write the source task's `work/discovered-routed.md` with the receipt, new or
   reused task ids;
6. stage only those explicit paths, commit with
   `Route follow-ups from <source-task-id>`, then run the workspace helper's
   `publish` command.

The worker must retry an ordinary concurrent-master race using the helper. A
semantic conflict or unavailable remote preserves the slot commit and stops
the scheduler. The routing marker makes later `/continue` runs idempotent.
Refresh queue JSON after routing, then resume the main loop.

## Report

Return one compact run summary: inbox receipt if processed, tasks approved,
tasks blocked with exact unverified behavior, tasks newly claimed or left
queued, routed discoveries, elapsed time, and why the loop stopped. Make any
global hard stop, retained unsafe state, or incomplete verification visually
unmistakable. The human should not need to invoke another command merely to
advance to the next eligible task. Never include source or AI commit hashes;
task ids are the only durable locators.
