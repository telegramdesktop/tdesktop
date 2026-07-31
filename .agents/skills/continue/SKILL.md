---
name: continue
description: Continue autonomous Telegram Desktop development from the shared ai-tdesktop repository. Use when the user invokes $continue or /continue, asks Codex to keep working through the AI queue, or wants one command to resume the active task at the head of a frozen startup batch, drain matching queued work, or process the local inbox only when startup has no task work, while including follow-ups discovered from the batch but deferring unrelated tasks added mid-run.
---

# Continue AI Work

Act as the checkout-level scheduler. Choose one invocation mode at startup,
freeze its task batch, and keep looping only through that batch and follow-ups
discovered from its results. Do not drain unrelated tasks added while the run
is in progress. Delegate inbox planning and one-task execution; do not plan or
implement Telegram changes in this scheduler session.

This is the default development command and the successor to the old `task`
and `implement` workflows. Inbox processing may bootstrap an otherwise idle
invocation exactly once and owns request splitting and project routing;
`perform-task` owns context, planning, implementation, review, Debug build,
test-loop, evidence, and final publication.

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
preference such as "payments tasks first" reorders the shared tasks recorded
in the startup batch; it does not exclude the others. A restriction such as
"only the payments tasks" or "all tasks except projects X and Y" records only
matching unclaimed shared tasks.
Hints never exclude this checkout's active, blocked, or legacy-reserved work.
A restrictive hint that matches no shared task does not make an existing
queue look idle or permit inbox processing.

The recorded batch is invocation-local bookkeeping, not a reservation. Start
only one shared task at a time, finish or exceptionally block it, refresh
canonical state, then select another recorded id. Always resume this
checkout's `in-progress` task and previously blocked work before applying a
priority hint to new shared work, unless the user expressly asks to stop or
reassign it.

The presence of active work changes ordering, not batch scope. Unless the user
expressly asks to run only the current task, freeze the same matching startup
queue behind the active task that would have been recorded without active work.

## Freeze the invocation batch

Use the first clean, refreshed queue snapshot to choose exactly one mode
before starting, retrying, resuming, or performing a task. Create and record
these invocation-local values in the scheduler plan:

- `invocation_mode`;
- ordered `initial_batch_task_ids`;
- ordered `batch_task_ids`, initially equal to the initial batch;
- empty `discovered_task_ids`;
- empty `attempted_blocked`.

Do not write a batch file, claim the whole batch, or publish reservations.
Queue refreshes update task state but never add ordinary task ids to the
frozen batch.

### Mode 1: resume active work, then drain the selected snapshot

If `own_in_progress` contains this checkout's active task, choose `active`
mode and record that task id first. Then append the same queue tail that Mode 2
would record from the startup snapshot:

- every own blocked and legacy-reserved task, regardless of the hint;
- every unclaimed task under no hint or a preference, ordered with preferred
  matches first;
- only matching unclaimed tasks under a restrictive hint.

Record dependency-waiting tasks too. Finish the active task to an approved,
genuinely blocked, or global-hard-stop boundary, then continue through ready
recorded tasks one at a time. Only an explicit request to run just the active
task produces a one-item active batch.

### Mode 2: drain the existing queue snapshot

When there is no active task but any `own_blocked`, `own_todo`, or
`unclaimed_todo` task exists, choose `queue` mode. Record:

- every own blocked and legacy-reserved task, regardless of the hint;
- every unclaimed task under no hint or a preference, ordered with preferred
  matches first;
- only matching unclaimed tasks under a restrictive hint.

Record tasks even when their dependencies are not yet approved. They may
become ready as earlier batch members finish. The existence of any queue task
selects this mode before a restrictive hint filters unclaimed ids; an empty
filtered batch stops normally without processing the inbox.

Never invoke `process-inbox` in `active` or `queue` mode, including after the
recorded batch drains.

### Mode 3: bootstrap from the inbox

Choose `inbox` mode only when `own_in_progress`, `own_blocked`, `own_todo`,
and `unclaimed_todo` were all empty in the initial snapshot. Work owned by
another checkout is not work this checkout can drain and does not enter its
batch.

If `inbox_nonempty` is false, record an empty batch and proceed to normal
stop. If it is true, spawn one inbox worker with `fork_turns: "none"`.

Give the worker the source checkout path and instruct it to read and use
`.agents/skills/process-inbox/SKILL.md` completely. It owns exactly one inbox
transaction, may use the bounded planner delegation required by that skill,
must not implement tasks, and must return the receipt and created ids.

Wait in intervals no longer than 60 seconds. A timeout is not failure. Inspect
the saved target after every wake and validate the receipt plus refreshed
queue before proceeding. Record as the initial batch exactly the actionable
task ids routed by that receipt, whether newly created or reused. Never launch
a second inbox worker in this invocation. If it cannot publish durable AI
state, stop with the inbox transaction recoverable.

Inbox content that appears or remains after this startup transaction is left
untouched for the next `$continue` or `/continue` invocation.

## Drain only the frozen batch

Refresh queue JSON after every delegated operation and state transition. A
task that appears in a later refresh but is absent from `batch_task_ids` is
not eligible in this invocation. Do not substitute it when a batch task is
claimed concurrently, blocked by an external dependency, or otherwise
unavailable.

Before publishing any new canonical `Start` commit, require a startable
environment: a clean Telegram source checkout with clean submodules and no
unrelated untracked files, plus an existing
`out/Debug/test_TelegramForcePortable` golden account. A failed check is a
global hard stop before claiming; never reserve shared work this checkout
cannot immediately run. Resuming and retrying already-owned work keeps the
performer's own preflight rules instead.

### 1. Resume active batch work

If this checkout has an `in-progress` task whose id is in `batch_task_ids`,
select it and spawn one performer. Its task-scoped dirty AI artifacts are the
resumption handoff. There must be at most one active task. Stop on an active
task outside the batch instead of silently expanding the batch or stealing
ownership.

### 2. Retry recorded blocked work

Otherwise select the first ready task in `own_blocked` whose id is in
`batch_task_ids` and not in `attempted_blocked`. Readiness means every
dependency is `approved`. Add its id to the set, then reopen it locally:

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

### 3. Start recorded reserved work

Otherwise select the first ready legacy `todo` task already owned by this
checkout whose id is in `batch_task_ids`, and start it:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py start \
  --task <YYYY/MM/DD/slug>
```

The resulting canonical `Start` commit changes it to `in-progress`. Leave
legacy reservations with unfinished dependencies untouched and consider later
ready batch work.

### 4. Start recorded shared work

Otherwise select the first ready unclaimed `todo` task whose id is in
`batch_task_ids`, using the order recorded at startup. Start it with the same
helper command. `start` atomically assigns and activates the task, then
publishes its canonical `Start` commit before source work begins.

A concurrent start may mean another checkout won the task. Never overwrite
shared state or replace it with a task outside the batch; refresh and continue
with another recorded id.

### 5. Stop normally

Stop when none of these batch-scoped conditions exist:

- this checkout's active batch task;
- a ready recorded blocked task not attempted in this invocation;
- a ready recorded legacy-reserved task;
- a ready recorded unclaimed task.

Recorded tasks still waiting on dependencies outside the batch, tasks won by
another checkout, and a blocked task already attempted in this invocation
remain visible in the final summary but do not cause a busy loop. Tasks added
by another inbox run, checkout, or user after the startup snapshot are outside
the batch and wait for the next invocation.

Do not process or wait for the inbox at this point.

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
with independent work. A dirty source checkout, a file-lock build failure that
remains after `perform-task` exhausts the shared exact-checkout recovery,
missing test account, unsafe publication conflict, or comparable global safety
failure stops the loop. The first lock signature never stops the batch.

The missing `test_TelegramForcePortable` golden account is the only
portable-folder global stop. All live/real folder combinations must be
reconciled by `perform-task` according to the shared test-loop protocol.
Computer Use being unavailable because macOS is locked is never a scheduler
stop; the performer must continue with the in-binary overlay driver and
artifact-based assessment.

## Route discovered follow-ups

After every canonical `Approve` or `Block`, read `work/result.md`. Route before
selecting more shared work whenever it lacks `work/discovered-routed.md` and
either says `Discovered: present` or carries a non-`none` `Unverified:` value.
Both are unfinished work leaving the pipeline; the only difference is that
`Discovered:` names work nobody has started and `Unverified:` names behavior that
already shipped without proof. An approved task whose unverified behavior was
never routed is exactly how coverage debt becomes invisible, so the marker file
gates both.

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

Project assignment has a strong source-project bias. When the source task has
a project, assign each discovered implementation or verification task to that
same project by default, add it to the project index, and name the source task
in `depends_on` whenever its shipped code or behavior is a prerequisite, even
when it is already approved. State that code-lineage requirement in the new
task so it is not attempted on a branch without the project changes.

Detach a discovered task to another project or to `project: null` only when the
worker proves it remains coherent, implementable, and independently testable
with the source project's changes absent or reverted. Touching shared code,
serving another surface, or having a broader title is not proof: projects
record feature and code lineage, not exclusive file ownership. The discovery
receipt must record the concrete independence evidence. If the source project
is archived, restore it before adding the task. When the source task has no
project, apply the ordinary project-selection rules from `process-inbox`.

First apply the scope filter, before any disposition. A verification exists to
prove **the source task's own change**, so run the revert test on each entry: if
reverting that task's diff could not change the outcome, the entry is about
pre-existing behavior and no verification is created for it. Untested code the
run passed on the way, a neighbouring feature, a parameter range the acceptance
never named, a pre-existing bug the performer noticed: record the observation in
the receipt and stop there. If it deserves work it must earn its own task on its
own merits, through the ordinary discovered-follow-up planner and with its own
justification — never as coverage debt attributed to a task that did not create
it. This filter is what keeps a codebase far larger than the queue from
generating verification work without end.

Entries that survive the filter get exactly one of two dispositions, and the
receipt records which and why:

- **Routable** when the existing test account and checkout could close the gap
  and the run simply did not cover it. Create a `type: verify` task naming the
  exact behavior to prove; its acceptance is that verification, and it carries
  no implementation work of its own.

  This disposition should now be rare. `pipeline.md` requires a performer to
  close any gap its own checkout can measure by adding a test run while it still
  holds the context, the branch, the overlay and the build, rather than deferring
  it — so a routable entry means that bar slipped. Route it anyway, because the
  coverage is genuinely missing and the source run's context is gone, but state
  plainly in the receipt that the source run could have closed it in context.
  That sentence is the measurable signal that the pipeline is exporting its own
  test coverage into the queue; read a run of them as a defect to fix upstream,
  never as normal throughput.
- **Infrastructure-limited** when closing it needs something the project does
  not have — a second account, funded external value, real server-backed cloud
  state. Record it in the receipt only. Do not create a task that would be
  unstartable the moment it enters the queue.

Never resolve a surviving entry by deciding the behavior is probably fine. Once
an entry is in scope, the disposition is about who can verify it and when, never
about whether it is worth verifying. That rule governs the choice between the two
dispositions; it does not override the scope filter above, which asks a different
question — whether this task is the one that owes the measurement at all.

Write `type: verify` into that task's `state.yaml`. It is the only thing that
selects the verification profile in `perform-task`, so a verification created
without it silently runs the implementation pipeline against an empty diff.
Give it one specific measurable claim: a task that would need a source change to
satisfy its own acceptance is misrouted and belongs in an `implement` task.

Write the source task's diff into it as its scope boundary, naming that task and
what it changed, and state that the verification proves that change and nothing
around it. A verification inherits its parent's boundary; it does not get a wider
one by being about testing. Its acceptance criteria must all pass the revert test
against the parent's diff, and it may not enumerate a parameter range the parent's
acceptance never named.

A `verify` task's own follow-ups are always `type: implement`. When a
verification reports `Finding: deviation`, route the repair as ordinary
implementation work naming the measured expected and actual values, and cite the
verification as its evidence. Never route a second verification for a gap the
first one already measured; the measurement exists, so what is left is the fix.

After validating the discovery receipt, append only the task ids created from
that result to `discovered_task_ids` and `batch_task_ids`, preserving routing
order. This is the only way the frozen batch grows. Apply the same rule
transitively when a discovered task later reports its own follow-ups.
Deduplicated references to pre-existing tasks and unrelated tasks observed in
queue refreshes do not join the batch.

## Report

Return one compact summary: invocation mode, initial batch ids, discovered ids
added to the batch, inbox receipt if processed, tasks approved, exceptionally
blocked tasks with exact unverified behavior and retry status, recorded tasks
left queued, unrelated new tasks deferred to the next invocation, routed
discoveries, infrastructure-limited coverage gaps recorded but not routed,
archived projects, any discarded interrupted-worker leftovers,
elapsed time, and why the loop stopped. Make any global hard stop or unsafe
state unmistakable. Never include source or AI commit hashes; task ids are the
only durable locators.
