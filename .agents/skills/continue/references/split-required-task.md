# Route a split-required task

Run this only from the `continue` scheduler for one task already published as
`status: split-required`. It is a deep task-routing transaction, not task
implementation. Inspect Telegram source and the retained implementation, but
do not edit, reset, stash, commit, build, or test Telegram source.

## Read the retained task

Read the source task's complete `task.md`, project context, dependencies,
`work/context.md`, `work/plan.md`, `work/split-proposal.md`, `work/result.md`,
and `work/carried-work.json`. Inspect the complete retained source diff and
the relevant adjacent code. The proposal is input, not a set of titles to copy
blindly: refine it into the smallest independently shippable and independently
testable product boundaries.

Each replacement must have one useful outcome, a direct acceptance oracle, and
enough self-contained context to run without this routing session. Split at
network, persistence, engine, ownership/lifetime, lifecycle, UI, platform, or
evidence boundaries when they can stand on their own. Keep inseparable APIs and
their only callers together. Add a final integration task only when integration
has behavior not already proved by the component tasks.

Preserve the source task's project by default. A replacement may leave that
project only when it remains coherent, implementable, and testable with the
project changes absent. Preserve the source task's existing dependencies where
their code is still required. Order replacement dependencies by actual shipped
code or behavior, not by the order in which the plan happened to mention them.

## Preserve existing implementation

`work/carried-work.json` records whether source implementation exists and seals
the owned working diff. When it says `implementation: retained`, designate
exactly one first replacement as the implementation carrier. Shape that task so
the whole retained diff is inside a coherent boundary it can inspect, correct,
review, build, and test. If the diff spans the future slices, a real first task
that stabilizes and proves the shared foundation is appropriate; an unreviewed
checkpoint or a task whose only result is storing the patch is not.

The carrier's `task.md` must name the split source task, tell the performer to
read its retained artifacts and complete source diff, and state which current
implementation it adopts. Later replacements depend on the carrier whenever
they consume that code. Create the carrier initially as pristine unclaimed
`todo`; the publication helper transfers its reservation and
`owned-paths.txt`. Do not copy phase conclusions as approvals: the carrier runs
its own focused planning, complete review, and evidence loop over the retained
implementation.

Do not discard existing implementation merely because a cleaner decomposition
would have started differently. Do not reset it, hide it in an ignored patch,
or split it into unreviewed commits during routing. If no coherent carrier can
own it, stop with the split-required task and source state untouched and report
the exact conflict for a human decision.

When `implementation: none`, create no carrier and leave every replacement
unclaimed.

## Write replacement state

Create at least two dated replacement task directories using the ordinary
`process-inbox` task and state schemas. Every replacement uses `type: implement`
and names the split receipt in `inbox_receipt`. Before publication every state
is pristine unclaimed `todo` and omits or sets `carried_from: null`; the helper
sets the carrier's durable ownership transfer.

Write one receipt under `receipts/YYYY/MM/DD/` containing:

- the split source id and why one campaign was incoherent;
- every replacement id, shipped boundary, acceptance oracle, and dependency;
- the implementation carrier or `none`;
- how every original acceptance criterion and retained source path is owned;
- every changed dependent and project link.

Replace the source task's link in its project index with the replacement links
in dependency/routing order. Rewrite every live task dependency on the source
to the exact successor or successors that provide the needed behavior. A
retired split id must remain in neither a live dependency nor a project index.
Do not edit the source task directory; the helper replaces only its
`state.yaml` with a sealed multi-target `split.yaml`.

## Validate and publish

Check the complete dependency graph, task links, projects, receipt, native text
format, and absence of persisted commit hashes. Then publish through:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py split-publish \
  --source-task <source-id> \
  --receipt <receipts/YYYY/MM/DD/split-receipt.md> \
  --replacement <first-id> \
  --replacement <next-id> \
  [--implementation-carrier <first-id>] \
  --path <tasks/source-id> \
  --path <tasks/first-id> \
  --path <tasks/next-id> \
  --path <each-changed-project-dependent-or-receipt-path>
```

Use the host's available Python 3 command. The helper rechecks the sealed source
worktree, replacement state, receipt, project indexes, full dependency graph,
and retained source-task digest. It publishes `Split <source-id>`. With a
carrier, it leaves the source refs and working implementation intact and makes
the carrier a checkout-owned `todo`; the scheduler must select it first and use
the normal `start` command, which rechecks the worktree seal, transfers the refs,
and publishes the carrier's canonical `Start`. Without a carrier the helper
removes the obsolete source refs after publication.

On an ordinary publication race, retry through the helper. Preserve an
unpublished split commit on semantic conflict or remote outage. Report the
source id, ordered replacements, carrier, receipt, publication state, and any
hard stop; never report commit hashes.
