# Consolidate pending AI tasks

Use one fresh leaf worker to reduce fixed per-task execution cost after newly
discovered follow-ups reach canonical AI state. Prefer a smaller queue when one
context pass, plan, review, build, fixture, and test run can prove several close
requests without weakening any of them.

## Contents

- [Inputs and boundary](#inputs-and-boundary)
- [Inventory and eligibility](#inventory-and-eligibility)
- [Aggressive merge decision](#aggressive-merge-decision)
- [Build replacement tasks](#build-replacement-tasks)
- [Traceability and validation](#traceability-and-validation)
- [Publish and return](#publish-and-return)

## Inputs and boundary

Receive the source checkout, AI slot worktree, checkout tag, the source task's
`work/consolidation-pending.md`, and the scheduler's effective ordered batch
ids. When a current invocation batch is already frozen, its `batch_task_ids`
are authoritative. Only recovery before a new batch is frozen uses the marker's
Batch list, which already includes every Created id. Read the discovery source
task and newly routed ids from the marker. Do not delegate. Do not read or
modify Telegram source, build, test, inbox, claim, or task execution state.
Work only in the clean checkout-specific AI slot after the discovery-routing
commit has published.

This is one queue-wide optimization pass, not another discovery planner. It may
replace compatible unclaimed tasks and update their direct dependents, project
indexes, and one new consolidation receipt. It must not invent new work, alter
approved history, create or move projects, or change the meaning of a request.

## Inventory and eligibility

Refresh canonical AI state. Inventory every task's id, status, ownership,
project, dependencies, and tracked task-directory contents. Partition all
mergeable work by this exact key:

1. the same project slug, or `project: null` for every member;
2. the same scheduler membership: every member is in the current batch, or no
   member is in it.

Never merge across projects, between a project and standalone work, or across
the frozen-batch boundary. Named projects and standalone work are separate
lineages even when their files overlap. Every unfinished task uses
`type: implement`; historical approved task types never enter a candidate set.

A candidate must be `status: todo`, `claimed_by: null`, with `claimed_at`,
`claim_order`, `lease_until`, and `phase` all `null`. It must have no tracked
`work/` or `evidence/` and no unexpected task-local artifact suggesting someone
has begun it. Inputs are allowed only when every pertinent file can be copied
and relinked without loss.

Read `task.md` and `state.yaml` completely for every member of each partition
containing at least two candidates. Read that project's `project.md` and
`tasks.md`, or the complete standalone candidate set. Inspect dependency and
reverse-dependency state. A candidate referenced by an `in-progress`, `blocked`,
approved, claimed, or otherwise non-mergeable task is ineligible; unclaimed
`todo` dependents may be rewritten atomically with the replacement.

Before selecting a cluster, simulate its replacement in the complete dependency
graph: remove the cluster, add the stable union of its external dependencies,
and rewrite every eligible dependent. Reject a cluster if the replacement would
depend directly or transitively on one of those dependents or if the simulated
graph contains any cycle. Dependency similarity never overrides this rule.

## Aggressive merge decision

Within an eligible partition, bias toward merging. Two or more tasks belong in
one replacement when they are close enough that a single execution can reuse a
material part of its context, implementation or measurement setup. Strong merge
signals include:

- the same component, control flow, surface, source seam, or expected files;
- one fixture, account state, overlay, process lifetime, or UI navigation can
  exercise all acceptance criteria;
- one implementation naturally establishes several requested invariants;
- coverage tasks measure the same parent diff, state machine, or tightly
  related surfaces from one instrumented run;
- the combined work can use one coherent plan, review, Debug build, and test
  loop instead of merely running unrelated jobs back to back.

Merge more than two tasks whenever those signals hold for the whole cluster.
The default question is why the tasks still need separate multi-hour pipelines,
not why they happen to resemble each other.

Keep tasks separate when the merged title and plan would be artificial, the
parts need independent designs or incompatible fixtures, one part materially
interferes with another's measurement, the work has a real sequential product
boundary, or the result would no longer fit one normal adaptive implementation
pass. Same project alone is not enough. Record close candidates
left separate and the concrete reason; do not use vague labels such as
"unrelated" or "too large".

For coverage clusters, retain each claim's own parent-diff boundary and revert
test. A union of dependencies or paths is not a new scope boundary. Combining
them saves setup; it never widens what shipped behavior each source task owes.
If a measurement finds a deviation, the adaptive task may repair it in the same
run.

## Build replacement tasks

Create one new dated task for each selected cluster. There is no `superseded`
status, but old task ids are durable: never delete their directories. Use a
concise imperative slug with the normal same-day collision suffix and write
canonical `task.md` plus `state.yaml`:

- `status: todo`, `type: implement`, the shared `project`, and all ownership and
  phase fields `null`;
- `created` equal to the local consolidation date;
- `depends_on` equal to the stable union of external source dependencies, with
  duplicates and every internally superseded id removed;
- `inbox_receipt` pointing to the new consolidation receipt.

Immediately before retirement, obtain each source specification fingerprint
from the helper while its live `state.yaml` still exists:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py task-content-digest \
  --task <source-task-id>
```

Retire each source by removing only its `state.yaml`, retaining its original
`task.md` and `input/`, and adding this exact `superseded.yaml`:

```yaml
superseded_by: YYYY/MM/DD/replacement-task
receipt: receipts/YYYY/MM/DD/consolidation-receipt.md
type: implement
project: project-slug
content_sha256: <helper-output>
```

Use `type: implement` and `project: null` where appropriate. The queue sees
only directories with `state.yaml`, while the workspace resolver follows
`superseded.yaml` chains. This preserves old receipt links, deduplication paths,
human bookmarks, original wording, and inputs without leaving duplicate live
work. The digest covers every retained file path and byte except `state.yaml`
and `superseded.yaml`; post-rebase validation rejects a late task or input edit.
Never put execution artifacts into a retired directory.

Organize the combined body and acceptance by named parts when that keeps source
boundaries visible. Preserve every unique acceptance criterion and every
load-bearing constraint, prerequisite, safety rule, visual basis, fixture fact,
and input. Exact duplicate criteria may collapse to the strongest version only
when the receipt maps every source criterion to it and explains why nothing was
lost. Never generalize precise readings into a weaker umbrella criterion.

For a combined coverage task with several parent tasks, give each part its own
scope boundary and dependency statement. Explicitly state that their union is
not the boundary of any individual claim.

Copy supplied files into the replacement's `input/` using collision-safe names,
rewrite all links, and map every old file to the new path in the receipt. If an
input cannot be preserved, exclude that cluster. Rewrite eligible external
dependents' dependency lists and prerequisite prose from old ids to the new id.

For a named project, replace the old links with the new link in `tasks.md` and
make nearby durable narrative coherent. Do not store live status there. Do not
edit earlier receipts, approved task artifacts, or the discovery-routing marker;
they are immutable history. Repeat the complete old-to-new mapping in the
replacement task, each durable alias, and the consolidation receipt.

## Traceability and validation

Write one concise receipt under `receipts/YYYY/MM/DD/` named for the checkout
and consolidation time. Include:

- trigger source task, local time, checkout tag, and newly routed ids;
- the eligible inventory and every selected or rejected close cluster;
- exact old-to-new mapping, project, and batch-membership class;
- shared setup that justifies each merge and the fixed-cost saving it creates;
- per-criterion and load-bearing-context accounting;
- dependency unions, dependent rewrites, inputs, project-index changes, and all
  paths created, changed, or removed.

Keep the proposal in worker context or an ignored temporary file. Immediately
before writing tracked files, refresh canonical state and re-read every source
and rewritten dependent. If any status, owner, project, dependency, or
tracked task contents changed, write nothing and return `RACED`, leaving the
pending marker for a later invocation. Apply all selected clusters only after
this safety check; never produce a partial consolidation.

Validate that every replacement is an unclaimed `todo`, every source criterion
and input is accounted for, all dependencies and links exist, project
boundaries hold, no old id remains as a live dependency or project link, every
alias chain reaches a live task, every retained-content fingerprint still
matches, and the complete live dependency graph is acyclic. Preserve native
line endings without a BOM. Never record source or AI commit hashes.

Whether or not anything merged, remove `work/consolidation-pending.md` and write
`work/consolidation-complete.md` under the source task. It must name the source,
newly routed ids, local time, examined partitions, concrete reasons for close
candidates left separate, receipt or `none`, mappings or `none`, and exactly one
of `STATUS: MERGED` or `STATUS: NO_MERGE`. This marker is the durable no-repeat
boundary. Only explicit task, project, receipt, and source-marker paths may
change.

## Publish and return

Do not stage or commit manually. Publish through the dedicated helper, passing
every changed source task, retired task, replacement task, rewritten dependent,
project, and receipt path explicitly. Pass one mapping for every retired id:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py consolidate-publish \
  --source-task <source-task-id> \
  --receipt <receipts/YYYY/MM/DD/name.md> \
  --mapping <old-task-id>=<replacement-task-id> \
  --path <tasks/YYYY/MM/DD/source-task> \
  --path <tasks/YYYY/MM/DD/old-task> \
  --path <tasks/YYYY/MM/DD/replacement-task> \
  --path <receipts/YYYY/MM/DD/name.md>
```

For `NO_MERGE`, omit `--receipt` and `--mapping` and pass the source task path
covering the marker change. The helper rejects changes outside the explicit
paths, validates aliases, retained-content fingerprints, and the full dependency
graph, stages the paths, and commits once as
`Consolidate pending tasks after <source-task-id>`. After every fetch and rebase
it repeats validation immediately before its push; the push is the
compare-and-swap boundary. A concurrent task that still names a retired id, a
late source-specification edit, a new cycle, a semantic conflict, or a remote
outage therefore preserves the commit and returns `BLOCKED`; never force-push.
The generic `publish` command recognizes and applies the same validator when
resuming this commit.

Return only this compact contract to the scheduler:

```text
STATUS: MERGED | NO_MERGE | RACED | BLOCKED
Receipt: <path or none>
Mappings: <old -> new pairs or none>
Created: <count>
Retired: <count>
Net: <retired minus created>
```

`NO_MERGE` publishes only the completion boundary. Pre-commit `RACED` leaves the
pending marker intact and is safe to defer. `BLOCKED` is reserved for a committed
or otherwise unsafe publication state that the scheduler must preserve and
report.
