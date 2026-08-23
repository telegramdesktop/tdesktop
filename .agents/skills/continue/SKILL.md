---
name: continue
description: Continue autonomous Telegram Desktop development from the shared ai-tdesktop repository. Use when the user invokes $continue or /continue, asks Codex to keep working through the AI queue, or wants one command to resume the active task at the head of a frozen startup batch, drain matching queued work, or process the local inbox only when startup has no task work, while including and consolidating follow-ups discovered from the batch but deferring unrelated tasks added mid-run.
---

# Continue AI Work

When running in Grok Build, read `.grok/ai-workflow-adapter.md` completely
before any other host-specific delegation rule and apply its substitutions.

Act as the checkout-level scheduler. Choose one invocation mode at startup,
freeze its task batch, and keep looping only through that batch and follow-ups
discovered from its results. Do not drain unrelated tasks added while the run
is in progress. After routing new follow-ups, consolidate compatible unclaimed
work in a fresh leaf worker. Delegate inbox planning and one-task execution; do
not plan or implement Telegram changes in this scheduler session.

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
helper's `publish` command before selecting work. It recognizes a consolidation
commit and revalidates its aliases and complete dependency graph after every
rebase before pushing; never publish such a commit manually.

The canonical lifecycle is deliberately small:

- `todo` with `claimed_by: null` is ready shared work;
- `in-progress` with this `checkout_tag` is this checkout's one active task;
- `blocked` with this tag is a rare published unfinished boundary;
- `split-required` with this tag is a published performer result awaiting one
  scheduler-owned split transaction;
- `approved` is the only completed terminal state.

After routing, the retired source has no live state: its sealed `split.yaml`
records all successors and the optional implementation carrier. This is
terminal history, not another runnable status.

`Start` atomically assigns an unclaimed task and changes it to `in-progress`.
Normal phase artifacts remain local and uncommitted in the slot worktree.
`Approve` publishes all final AI artifacts and state in one commit. `Block` is
permitted only for a genuine exhausted task blocker,
not for an interrupted agent session. `Split-required` publishes the proposal
and seals any retained source work for the scheduler transaction. Never publish
`Claim`, phase checkpoint, or `Resume` commits. Existing claimed `todo` records from the older workflow
remain startable but do not justify creating new reservations.

Never infer ownership from an inbox receipt. Do not steal work from another
checkout. Moving an unfinished task to another checkout is a rare explicit
human reassignment that may restart the task and discard checkout-local phase
artifacts; it is never automatic scheduler behavior.

Before freezing a new invocation batch when no task is `in-progress`, handle
each entry in the queue JSON's `pending_consolidations` once. These durable
markers mean discovery routing published new tasks but its separate
consolidation pass did not finish. Spawn the consolidation worker described
below with the source task and batch ids recorded in the marker, then refresh
the queue. A repeated pre-commit race may remain pending for the next
invocation; record that marker as attempted and do not spin. If a task is
already active, its expected local phase files make the shared AI slot unsafe
for consolidation: freeze and finish that active task first, then recover all
pending markers at its clean canonical `Approve`, `Block`, or completed split
routing boundary before selecting more work.

After recovering pending consolidations and before freezing a new batch, route
this checkout's `own_split_required` task, when any, through the dedicated split
worker below. Refresh the queue afterward and freeze the resulting replacement
tasks at the front of the initial batch regardless of a scope hint; they replace
checkout work that hints cannot exclude. Record that startup split for the
invocation summary. A split-required task is exclusive checkout work: do not
start, retry, or resume another task while it remains unrouted. If its retained
source state cannot be transferred safely, stop with that state intact rather
than skipping it.

## Interpret scope hints

Treat text after `$continue` or `/continue` as optional natural-language
guidance for new shared work. Its own wording decides its strength. A
preference such as "payments tasks first" reorders the shared tasks recorded
in the startup batch; it does not exclude the others. A restriction such as
"only the payments tasks" or "all tasks except projects X and Y" records only
matching unclaimed shared tasks.
Hints never exclude this checkout's active, split-required, blocked, or
legacy-reserved work.
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
- empty `consolidation_mappings` and `consolidation_receipts`;
- empty `attempted_blocked`;
- `split_records`, initialized with any startup split transaction and otherwise
  empty.

Do not write a batch file, claim the whole batch, or publish reservations.
Queue refreshes update task state but never add ordinary task ids to the
frozen batch.

### Source-lineage gate

Before freezing the batch, inspect every prospective initial task's `task.md`
and dependencies. For every approved task whose shipped code is a prerequisite,
run:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py source-lineage \
  --task <task-id> [--require <explicit-source-task-id> ...]
```

`depends_on` requirements are included automatically. Pass `--require` for an
explicit source prerequisite named in `task.md` that old routing failed to put
in `depends_on`. Unfinished dependencies remain a readiness concern and appear
separately; this gate checks the history of approved source work.

If any prospective task reports `current_satisfies: false` at this startup
gate, pause before freezing, starting, retrying, resuming, or switching
branches. Report the current branch, missing source task ids, unavailable
commits, and compatible local branches, then ask the human whether to rebase,
bring the commit, switch the checkout, or change scope. Never create or route
an integration task, and never cherry-pick, rebase, merge, or switch branches
at this startup boundary. The exception is an already-active task whose saved
artifacts prove Phase 1 completed: it has crossed the task boundary, so resume
the performer and let the after-Phase-1 rule publish the task-local Block.

After the batch is frozen, rerun the same gate immediately before each Start,
Retry, or pre-Phase-1 resume. A mismatch first found here is recoverable queue
routing, not a task blocker: while the selected task has not completed Phase 1,
switch this checkout to a compatible existing local branch and continue the
same frozen batch. Require a clean source checkout and submodules, no owned or
disposable task overlay, no exact checkout executable, no source recovery refs
for work already begun, and verify with `git worktree list --porcelain` that the
branch is not checked out elsewhere. Prefer a compatible branch appearing for
the most remaining batch tasks; preserve recorded batch order. Do not create a
branch or cherry-pick, rebase, or merge. After `git switch`, refresh `queue`,
rerun `source-lineage` and `source-preflight`, then Start/Retry or resume. If no
safe compatible local branch exists, stop and ask the human.

### Mode 1: resume active work, then drain the selected snapshot

If `own_in_progress` contains this checkout's active task, choose `active`
mode and record that task id first. Then append the same queue tail that Mode 2
would record from the startup snapshot:

- every own blocked and legacy-reserved task, regardless of the hint;
- every unclaimed task under no hint or a preference, ordered with preferred
  matches first;
- only matching unclaimed tasks under a restrictive hint.

Record dependency-waiting tasks too. Finish the active task to an approved,
genuinely blocked, split-required, or global-hard-stop boundary, route any
split before continuing, then continue through ready recorded tasks one at a
time. Only an explicit request to run just the active
task produces a one-item active batch.

### Mode 2: drain the existing queue snapshot

When there is no active or split-required task but any `own_blocked`,
`own_todo`, or `unclaimed_todo` task exists, choose `queue` mode. Record:

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

Choose `inbox` mode only when `own_in_progress`, `own_split_required`,
`own_blocked`, `own_todo`, and `unclaimed_todo` were all empty in the initial
snapshot. Work owned by
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

Before publishing any new canonical `Start` commit, require a clean Telegram
source checkout with clean submodules and no unrelated untracked files. The one
exception is the checkout-owned first replacement whose `carried_from` field
and source `split.yaml` designate it as the implementation carrier: start it
with the retained source state intact, and let the helper revalidate the sealed
worktree before transferring task refs. Do not require a Telegram executable,
portable account, desktop, Docker daemon, or
other instrument before assessment selects it. The performer gates every
selected instrument before using it and records an unavailable platform or
stage precisely instead of preventing unrelated task work from starting.

### 1. Resume active batch work

If this checkout has an `in-progress` task whose id is in `batch_task_ids`,
select it and spawn one performer. Its task-scoped dirty AI artifacts are the
resumption handoff. There must be at most one active task. Stop on an active
task outside the batch instead of silently expanding the batch or stealing
ownership.

### 2. Start a carried implementation

Otherwise select the first ready checkout-owned `todo` task in
`batch_task_ids` whose `carried_from` field names a retired split task. It must
be the first replacement and match that task's `implementation_carrier`.
Start it with the normal helper command. The helper verifies the source
worktree seal, transfers the old task's base/green/run refs, publishes the
carrier's canonical `Start`, and removes the obsolete refs. If any check fails,
hard-stop with both the carrier and retained source state intact; do not start
another batch task around it.

### 3. Retry recorded blocked work

Otherwise select the first ready task in `own_blocked` whose id is in
`batch_task_ids` and not in `attempted_blocked`. Readiness means every
dependency is `approved`. Reopen it locally:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py retry \
  --task <YYYY/MM/DD/slug> [--require <explicit-source-task-id> ...]
```

This preserves its ownership, source recovery refs, plans, reviews, tests,
result, and evidence while changing the slot worktree back to local
`in-progress`. It publishes no `Resume` commit. Spawn its performer at the
first incomplete validated boundary.

Add the id to `attempted_blocked` only if the performer later publishes a
genuine new `Block` boundary under the validation below. A test-campaign cap,
`TEST_FLAW`, blank/missing evidence, or another recoverable harness failure is
not genuine and does not consume this invocation's blocked retry.

### 4. Start recorded reserved work

Otherwise select the first ready legacy `todo` task already owned by this
checkout whose id is in `batch_task_ids`, and start it:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py start \
  --task <YYYY/MM/DD/slug> [--require <explicit-source-task-id> ...]
```

The resulting canonical `Start` commit changes it to `in-progress`. Leave
legacy reservations with unfinished dependencies untouched and consider later
ready batch work.

### 5. Start recorded shared work

Otherwise select the first ready unclaimed `todo` task whose id is in
`batch_task_ids`, using the order recorded at startup. Start it with the same
helper command. `start` atomically assigns and activates the task, then
publishes its canonical `Start` commit before source work begins.

A concurrent start may mean another checkout won the task. Never overwrite
shared state or replace it with a task outside the batch; refresh and continue
with another recorded id.

### 6. Stop normally

Stop when none of these batch-scoped conditions exist:

- this checkout's active batch task;
- a ready recorded carried implementation;
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
Own this task until it is approved, genuinely blocked, split-required, or
reaches a global hard stop. You may use the bounded leaf delegation required by
the skill. Do not select or start another task.
```

The performer is stateful. Never duplicate it. Poll at no more than 60-second
intervals, distinguish progress from completion using its task artifacts, and
follow up with the same target if it becomes idle before a valid boundary.

After it returns, require one of:

- source checkout clean at the recorded run tip and task `approved` on
  canonical AI master;
- source checkout clean and task exceptionally `blocked` on canonical master,
  with exact unverified behavior;
- task `split-required` on canonical AI master with `work/split-proposal.md`
  and `work/carried-work.json`, leaving every retained source change and task
  ref sealed for transfer; or
- a clearly reported global hard stop, leaving the task `in-progress` and all
  task-scoped local state recoverable for the next invocation.

A rescope result stops task performance and is not retried, approved, blocked,
or routed as an ordinary discovered follow-up. The performer's publication is
the durable request for the scheduler-owned transaction below; it is not
permission to delete source or flatten several successors into one alias.

## Route a split-required result

When a batch performer publishes `split-required`, immediately spawn one fresh
split worker with `fork_turns: "none"`. Give it `source_root`, `slot_worktree`,
`checkout_tag`, the source task id, and the current ordered batch. Tell it not
to delegate and to read
`.agents/skills/continue/references/split-required-task.md` completely. It may
inspect Telegram source and edit/publish AI task, project, dependency, and
receipt state; it must not edit, reset, stash, commit, build, or test Telegram
source.

Validate its canonical `Split <source-id>` result and refreshed queue. Replace
the source id in `batch_task_ids` at its existing position with the ordered
replacement ids, removing duplicates; leave `initial_batch_task_ids` unchanged.
Append the source, replacements, carrier, and receipt to `split_records`. The
replacements are part of this invocation because they replace an existing batch
member, not because later queue refreshes normally expand the frozen batch.

When a carrier exists it must be first, checkout-owned `todo`, and name the
source in `carried_from`; select it through the carried-implementation step
before any blocked, reserved, or shared task. Starting it performs the sealed
source-ref transfer. When no carrier exists, all replacements are ordinary
unclaimed `todo`. A split publication race is retried normally. A semantic
conflict, unavailable remote, changed worktree seal, or incoherent carrier is a
global hard stop with the source result and implementation left recoverable.

Before accepting a canonical test block, read `work/result.md` and
`work/test.md`. It is genuine only when the verdict is not `TEST_FLAW`, does
not cite `MAX_TEST_RUNS` or a missing/blank capture as the blocker, and
`work/test.md` contains `## Recovery exhaustion`; the separately documented
Computer Use infrastructure-unavailable verdict is the only exception to the
section requirement. If an older or concurrently finishing performer
published a boundary that fails this check, immediately `retry` it in this
same invocation, keep it out of `attempted_blocked`, and spawn one fresh
performer at the focused test-recovery boundary. Preserve all positive
evidence and rerun only unmet checks. New performers cannot normally publish
such a boundary because `workspace.py finish` enforces the same rule; this is
defense for legacy state.

An interruption or environment stop never becomes a convenience `Block`.
After a genuine `Block`, add the task id to `attempted_blocked` and continue
with independent work. A source-lineage mismatch first proven after Phase 1 is
such a genuine task-local Block: continue with batch tasks that do not depend
on it and whose own lineage gates pass. A pre-Phase-1 lineage stop is not a
Block or global hard stop; apply the safe mid-queue branch-switch rule above and
resume the same performer. A dirty source checkout, a file-lock build failure that
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
must not edit Telegram source, start tasks, or implement work. Give it the
current ordered `batch_task_ids` for the pending consolidation marker.

The worker must deduplicate existing tasks, create independently testable
unclaimed `todo` tasks and justified project updates, write a discovery
receipt, and write the source task's routing marker. When it creates at least
one task, it must also write `work/consolidation-pending.md` under the source
task, recording the source id, newly created ids, and the post-routing batch:
the scheduler's current ordered `batch_task_ids` followed by the newly created
ids in routing order with duplicates removed. The marker is part of the routing
commit and makes the separate pass resumable after a crash. It stages only
those paths, commits
`Route follow-ups from <source-task-id>`, and publishes with the workspace
helper. Retry ordinary concurrent-master races; preserve a semantic conflict
or unavailable-remote slot commit and stop.

Never route discovered work whose sole purpose is moving an existing commit to
another branch: no backport, forward-port, cherry-pick, rebase, merge, or
branch-sync task. Record that request or observation in the discovery receipt
only, naming the source task and desired branch when known. A real product
follow-up may depend on the source task, but `depends_on` carries that lineage;
do not create an integration companion task.

Use this stable marker shape so a context-free worker can recover it:

```markdown
# Pending task consolidation

Source: <source-task-id>
Created:
- <new-task-id>
Batch:
- <ordered-post-routing-batch-task-id>
```

Project assignment has a strong source-project bias. When the source task has
a project, assign each discovered task to that
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

First apply the scope filter, before any disposition. A coverage follow-up
exists to prove **the source task's own change**, so run the revert test on each entry: if
reverting that task's diff could not change the outcome, the entry is about
pre-existing behavior and no coverage task is created for it. Untested code the
run passed on the way, a neighbouring feature, a parameter range the acceptance
never named, a pre-existing bug the performer noticed: record the observation in
the receipt and stop there. If it deserves work it must earn its own task on its
own merits, through the ordinary discovered-follow-up planner and with its own
justification — never as coverage debt attributed to a task that did not create
it. This filter is what keeps a codebase far larger than the queue from
generating coverage work without end.

Entries that survive the filter get exactly one of two dispositions, and the
receipt records which and why:

- **Routable** when an available checkout or capable host can close the gap.
  Create an ordinary `type: implement` task naming the exact behavior to
  establish. It first measures the claim with the adaptive evidence loop. If
  the behavior deviates, it repairs and re-tests it in the same task. If the
  behavior already holds and no permanent change is warranted, it may approve
  as `Outcome: already-satisfied` with no source commit and with the measurement
  evidence retained.

  This disposition should be rare. `pipeline.md` requires a performer to
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

Every discovered task uses `type: implement`; assessment, not routing, chooses
its review and evidence depth. For a coverage follow-up, write the source
task's diff into it as its scope boundary, naming that task and what it changed.
Its acceptance criteria must all pass the revert test against that boundary and
must not enumerate a parameter range the source task never named. If a prior
coverage task already measured a deviation, route only the repair with the
measured expected and actual values; do not create another measurement of the
same gap.

After validating the discovery receipt, append only the task ids created from
that result to `discovered_task_ids` and `batch_task_ids`, preserving routing
order. This is the only way the frozen batch grows. Apply the same rule
transitively when a discovered task later reports its own follow-ups.
Deduplicated references to pre-existing tasks and unrelated tasks observed in
queue refreshes do not join the batch.

## Consolidate pending tasks after discovery

Whenever discovery routing publishes `work/consolidation-pending.md`, run one
fresh consolidation pass before selecting the next task. Do not run it for a
receipt-only routing or a routing that only reused existing tasks. Do not reuse
the performer or routing worker: spawn one disposable worker with
`fork_turns: "none"`, instruct it not to delegate, and give it `source_root`,
`slot_worktree`, `checkout_tag`, the pending marker, and effective batch ids.
Use the current frozen `batch_task_ids` when one exists; only recovery before a
new batch is frozen uses the marker's Batch list. The marker always supplies the
source task and newly created ids after scheduler context is lost.

Tell it to read
`.agents/skills/continue/references/consolidate-pending-tasks.md` completely and
own exactly one queue-wide consolidation pass. The worker may edit and publish
AI task, project, and receipt state, but must not touch Telegram source, build,
test, claim, start, approve, or block work. Wait and validate it like the routing
worker; keep its task-description scan and merge reasoning out of the scheduler
context.

A no-merge result still publishes `work/consolidation-complete.md` and removes
the pending marker, so it cannot be repeated after a restart. A pre-commit race
leaves the pending marker intact; refresh the queue, record it as attempted for
this invocation, and continue without treating the optimization as a blocker.
If the worker created a commit that cannot be published safely, preserve it and
hard-stop exactly as for discovery routing.

At every clean canonical `Approve`, `Block`, or completed split routing
boundary, process any older
pending marker deferred by an active startup task before selecting more work,
then process the marker just created by that task's routing. Attempt each marker
at most once per invocation.

For each published old-to-new mapping, rewrite `batch_task_ids` by placing the
replacement at the earliest position occupied by any of its sources and removing
the other source ids and duplicate replacement ids. Do not add a replacement
when none of its sources was in the batch. Apply the same replacement and
deduplication to `discovered_task_ids`; leave `initial_batch_task_ids` unchanged
as the startup record. Append the mapping and receipt to the invocation-local
consolidation records, refresh canonical state, and only then select more work.

## Report

Return one compact summary: invocation mode, initial batch ids, discovered ids
added to the batch, inbox receipt if processed, tasks approved, exceptionally
blocked tasks with exact unverified behavior and retry status, recorded tasks
left queued, unrelated new tasks deferred to the next invocation, routed
discoveries, split-required sources with ordered replacements and carriers,
infrastructure-limited coverage gaps recorded but not routed,
consolidation no-merge results or receipts, old-to-new mappings, the net
task-count saving, archived projects, any discarded interrupted-worker leftovers,
elapsed time, and why the loop stopped. Make any global hard stop or unsafe
state unmistakable. Never include source or AI commit hashes; task ids are the
only durable locators.
