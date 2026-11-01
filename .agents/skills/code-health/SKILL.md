---
name: code-health
description: Measure Telegram Desktop code health on freshly fetched origin/dev, keep a findings ledger with stable identities, survey one hot area in depth, and turn findings into proposed quality tasks with proportional proof. Use for the scheduled code-health run, an explicitly requested area survey or area map, or checking a change against the quality ratchet.
---

# Code health

Turn agent compute into lasting maintainability: later tasks that take fewer
hours and review rounds, fewer regressions, and no growth in build work or
binary size. This skill measures, proposes and checks. It never edits product
code; improvements land as ordinary queue tasks under
[the task classes](references/classes.md). Deletions, moves and merges of
identical copies (Q0–Q2) run perform-task's short mechanical lane; tests,
reshapes and redesigns run the full pipeline.

Run every command from the repository root. The scripts need only Python 3
and Git, and finish in seconds.

## Where it runs and where its state lives

Any Telegram Desktop checkout with `ai-tdesktop` beside it runs code health;
no checkout is special. The shared state lives in ai-tdesktop's
`code-health/`, so every checkout and machine sees one ledger and one set of
decisions:

- `ledger.json`: every finding with its first-seen and resolved dates, plus
  the scanned commit, the totals and `run`, the latest run's directory;
- `history.jsonl`: one line of totals per run;
- `runs/<stamp>-<commit>/`: the scan's `report.md` and the run's
  `summary.md` and `proposals.md`, plus `harness.md` and `harness.json`;
- `ignored.json`: findings the owner declined, with the reason and date;
- `audits.json`: audit results and suspended class and area pairs;
- `surveys/<area>/<date>/` and `atlas/<stamp>-<commit>/`: drafts for the
  owner;
- `.local/`, ignored: each machine's cache of raw scans by commit.

Read the state from `../ai-tdesktop/code-health/`. Write it only in this
checkout's code-health worktree, and publish it like other AI state:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py health-ensure
python3 .agents/skills/process-inbox/scripts/workspace.py health-publish \
    --message "<subject>" [--drop-on-conflict]
```

`health-ensure` prints `health_state`, the worktree's `code-health/`
directory, after bringing it to canonical master. When it reports
`health_dirty` or `health_unpublished`, an earlier run here was interrupted:
publish the leftovers with `health-publish --message "Record an interrupted
code health run" --drop-on-conflict`, then ensure again. `health-publish`
commits only `code-health/` and pushes through the same rebase-and-retry
path as task state.

## Scheduled run

Once a day, from any checkout:

1. Run `health-ensure`, then the scan below with `--state <health_state>`.
   A scan that prints `skipped` found today's run already recorded: stop.
2. Claim the day with `health-publish --message "Record code health scan of
   <commit>" --drop-on-conflict`. `dropped: true` means another checkout
   published today's scan first: stop without routing anything.
3. Read the daily harness inventory and follow
   [helper lifecycle review](references/harness-lifecycle.md); review retirement
   candidates at most once a week. Audit, survey when one is due, then write the summary and proposals
   (below) and route what the trust rules allow.
4. Publish with `health-publish --message "Publish code health report for
   <date>"`.

The scheduled run is `/code-health scan route` in Claude Code or
`$code-health scan route` in Codex.

## Scan

```bash
python3 .agents/skills/code-health/scripts/health.py scan --ref origin/dev --fetch --state <health_state>
```

It reads the exact `origin/dev` commit from Git objects, including each
submodule at its pinned commit, without touching the working tree, so the
checkout may hold unrelated work. A submodule commit missing locally is
skipped and named in the notes. A failed fetch is a failed run; never present
an older ref as fresh. `--state` records the scan in the ledger, history and
run report, at most once a UTC day unless `--force`; findings listed in
`ignored.json` stay in the ledger as `ignored`. Only a scan of an exact ref
is recorded. Without `--state` (and, for testing, without `--ref`) the report
goes to a temporary directory and the ledger is untouched; label such a run
as a test.

What the scan measures: file and class size, function length, churn and
hotspots (size × churn), duplicated code, unused-function candidates,
include-graph build cost (lines each unit compiles beyond its precompiled
header, and what an edit to each header recompiles), include edges that
cross CMake target boundaries, headless test coverage, stale area maps, and
agent cost per area (hours from `Start` to `Approve` and review rounds of
tasks whose commits touched the area, read from `../ai-tdesktop`).
It also records harness helper age, independent archived task consumption,
internal consumers, open-task references, source size and documentation weight.
The [harness reference](references/harness-lifecycle.md) defines the coverage
limits and the 90-day review triggers. Sparse use alone is never Q0 proof.

Scan daily; propose when the code-health budget has room; survey at most
one area a week. Surveys and audits read the code at the scanned commit, not
the checkout's working tree:

```bash
python3 .agents/skills/code-health/scripts/health.py worktree
python3 .agents/skills/code-health/scripts/health.py worktree --remove <path>
```

The first prints the path of a temporary detached checkout of the latest
published scan's commit, with its `lib_*` submodules; remove it when done.

## Atlas (whole tree)

Follow [the atlas procedure](references/atlas.md) when the owner asks for it
or `Telegram/SourceFiles/AGENTS.md` does not exist yet: `health.py atlas`
builds the data skeleton, agents describe and consolidate the areas, the
other provider verifies, and the owner approves the draft in words before it
is committed. Once it exists, `health.py` names areas by it, and the scan
reports files no area matches and patterns that match nothing.

## Survey (one area)

Follow [the survey procedure](references/survey.md). Pick the area with the
highest product of hotspot and median task hours that has no survey newer
than its last 100 commits, unless the invocation names one. A survey drafts
or amends the [area map](references/maps.md) and writes proposals; it changes
no source. To survey a named area now, run `$code-health survey <area>` (or
`/code-health survey <area>`) in any checkout; it writes to `health_state`
and publishes with `health-publish`.

## Owner decisions

The owner reads the latest run's `summary.md` and decides in a conversation
in any checkout. The agent reads the state from `../ai-tdesktop/code-health/`,
then:

- **Route** named proposals or survey defects: append each, verbatim, to
  `../ai-tdesktop/inbox/inbox.md` as a separate request. This is the owner's
  word that Q3, Q4 and Q5 need; `process-inbox` makes them tasks.
- **Approve** a drafted map or invariants: write the approved text, with the
  owner's changes, to the area's `AGENTS.md` in the source checkout, date
  each approved invariant, and commit it as `[ai] Add the <area> map`.
- **Decline** a finding: add it to `ignored.json` in `health_state` with the
  reason and date, and publish.

## Check (every change)

```bash
python3 .agents/skills/code-health/scripts/health.py check --base <base> [--staged] [--strict]
python3 .agents/skills/code-health/scripts/health.py moves --base <base> [--head <rev>]
python3 .agents/skills/code-health/scripts/health.py verify --base <base> <finding ids>
```

`check` compares the change with its own base, so no stored baseline is
needed; untracked files count as added. Errors: a new include into a CMake
target the including target does not link, when no include between those two
targets exists yet, or a new file over the size limit. Warnings: one more
include across an already crossed target pair, growing a file that is
already over the limit by more than a 20-line fix (an error under
`--strict`), a class over the method limit growing, added code that repeats
existing code, a header made materially heavier for the units that include
it, a removed function, class, style or language key still mentioned
somewhere, including platform files this host never compiles, and a removed
last use that leaves a function, field, class, style or language key with
only its definition. `perform-task` runs it before every review.

`moves` proves a Q1 change only moves code; it lists every added line that was
not removed, every removed line that did not reappear, and moved statements
whose order changed. `verify` reports each finding as `present`, `resolved`
(present at the base, gone now), `changed` (a clone replaced by a smaller
one) or `unknown` (not found at the base), comparing the working tree with
the base.

`health.py area <area>` prints everything the latest scan knows about one
area; surveys and task planners start there.

## Proposals

Select at most five proposals per run, and stop when ten code-health tasks
are already open in the queue (`task.md` with a `## Code health` section and
an open status).

- Skip findings whose files are named in an in-progress task's `task.md`
  or in this checkout's own `work/owned-paths.txt` (other checkouts publish
  theirs only at approval), findings already named by an open code-health
  task, and findings an approved code-health task listed under `Dropped:` in
  its `work/result.md` in the last 180 days, unless their metric has since
  doubled or the class bounds changed in a way that removes the reason.
- Prefer, in order: Q0 deletions; Q1 for heavy headers and oversized files
  in areas with high churn; Q2 for duplicated code that still changes. Q3
  tests for hot untested areas, Q4 and Q5 are proposed only when their
  preconditions in `classes.md` hold, and never routed without the owner's
  word.
- One proposal is one class in one area, batched as `classes.md` says.
- Weigh areas by agent cost: high median hours or review rounds per task
  means structure is slowing every task there.
- Code unchanged for 180 days gets only Q0 and Q3 proposals.

Each proposal is a self-contained request:

```markdown
# <Imperative title>

<One paragraph a performer can act on: what changes, where, and why it pays
off, with the finding's numbers.>

## Code health

- Class: Q<n>
- Area: <area>
- Findings: <ledger ids>
- Metric: <before> -> <target>
- Proof: <the class proof, as commands>

## Acceptance

- <Observable, checkable outcomes.>
```

A Q0–Q2 proposal's acceptance always includes: "Every listed finding is
resolved under its class proof or recorded under `Dropped:` with the reason
it cannot be resolved mechanically."

Write them to `proposals.md` in the run directory. They enter the queue only
through `process-inbox`. When this invocation is authorized to route (the
scheduled run, invoked with `route`, or the owner's request), append the
proposals the trust rules at the end of `classes.md` allow to
`../ai-tdesktop/inbox/inbox.md` as separate requests, and report the rest for
the owner. Never route a class an audit has suspended in that area.

## Audit

Each scheduled run picks at random one code-health task approved since the
previous audit. A fresh agent with no task context checks it against its
class: the proof was run and supports the claim, behavior did not change, and
the metric moved as stated. Use the other provider when the host can run
one: on Claude Code a Codex CLI leaf under
[Codex CLI leaves](../../shared/codex-cli-leaves.md) (`--cwd <scan worktree>
--sandbox read-only`); otherwise a fresh native agent. Mechanical tasks had one
reviewer, so this sample is their second look. Record the result in
`audits.json` in `health_state`, with suspended class and area pairs. A
failed audit suspends routing of that class in that area and becomes an
ordinary repair task.

## Report contract

Save `summary.md` and `proposals.md` beside the run's `report.md` in
`health_state`. The summary has two blocks, in order, both present even when
empty:

1. **Got worse or needs a decision**: new layering violations, newly
   oversized files, build work up more than 1% since the previous run, new
   duplicated blocks of 20+ lines, failed audits, invariants or maps awaiting
   approval, new or changed harness retention/documentation findings, and this
   run's proposals with their routing state. Link `harness.md` and name any
   blocked or deferred helper review; unavailable usage is not zero use.
2. **Trends**: changes in the totals since the previous run and over 30 days,
   the five areas with the highest agent cost, and progress of open
   code-health tasks. Include helper count, source lines, mandatory README
   bytes, on-demand documentation bytes and retention-candidate changes; the
   first run of these metrics is a baseline, not growth from zero.

End with one coverage line: scanned ref and commit, skipped submodules, and
whether agent cost was available.

## Guardrails

- Metrics choose work; they never define success. Acceptance is the class
  proof plus the improvement the task named.
- No codebase-wide style or modernization sweeps. A convention is enforced by
  adding one mechanical check, not by rewriting files.
- Bugs found while measuring or surveying become ordinary tasks.
- A finding the owner declines goes into `ignored.json` with the reason and
  date; propose it again only if its metric doubles.
