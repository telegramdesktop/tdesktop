# Harness helper lifecycle

Read `harness.md` and `harness.json` on every scheduled run. They come from
the same freshly fetched source commit as the scan, even when the checkout
has local work. Do not substitute the checkout's HEAD or delete a helper
while scanning. A helper's age and sparse archived use prompt a retention
decision; they do not establish dead code or measured build/runtime cost.

## Daily inventory

`health.py scan` writes both artifacts beside `report.md`, and carries these
stable findings into the normal ledger:

- `harness-retention:test_<name>`: a reusable API is at least 90 days old
  and has no observed independent consumer in the last 90 days, or at most
  two observed lifetime consumers. Unknown consumers conservatively count
  against those triggers. Known open consumers hold routing.
- `harness-docs:Telegram/SourceFiles/test/README.md`: the mandatory guide
  reaches 8 KiB, is missing, or modules lack their indexed on-demand page.

Both are `decision` findings until review establishes an appropriate task
class. They share the existing five-proposal/ten-open-task budget,
deduplication, active ownership, dropped-finding and audit-suspension rules.
An unchanged candidate is the same finding, not another daily request.

Each module records its first addition in reachable source history, creation
task, kind, source lines, reference bytes, consumer task ids and dates, last
observed use, source callers, internal dependents, workflow references and
known open tasks. The scenario slot is excluded. Module age follows the
current file path; a rename or incomplete history needs manual confirmation.
Shallow source history cannot produce an age-based candidate.

Reuse counts distinct approved task records, not repeated calls or runs.
The scanner reads tracked final `work/*.patch` artifacts, retaining only
added/context C++ lines outside permanent helper implementations. Includes
and identifiers unique to a helper's header are textual evidence. Comments,
string literals and removed lines are not consumers. Dependency edges carry
consumers to internal helpers; direct counts remain separate in the JSON.

The creation task, tasks whose source commits are exclusively `[ai]`, and
self-test-only calls are kept out of product reuse. A task without source
commits reachable from the scanned ref is unclassified, not guessed from its
name. Missing approval dates and unclassified consumers stay explicit.
Pure self-test and internal modules are inventoried separately; a lack of
direct product calls does not make them retirement candidates.

The archive is a lower bound: it omits discarded attempts, unsaved overlays,
and some indirect or workflow uses. No saved patch is not proof that a task
used no helper. Report the archive coverage, unknown classifications, missing
dates and read failures. An unavailable, changing or dirty canonical queue
leaves prior retention findings `unverified`, not resolved. Do not route them
from stale evidence. A data/schema change is a new measurement baseline;
do not describe its count change as source deterioration.

## Weekly retention decisions

Review newly eligible or materially changed candidates at most once per UTC
calendar week, alongside the existing proposal selection; this is not a
second area survey. Record the week, scanned commit, reviewed finding ids,
evidence and outcome in `harness-reviews.json` under `health_state`, and
publish it with the normal report. Check the canonical record before writing
so another checkout's completed weekly review is respected. Record explicitly
when there are no candidates. Do not route the same candidate again because
its age increased by another day.

Before proposing retirement:

1. Confirm the module's role from its header and implementation. Check its
   actual call sites, internal dependencies, registration/runner entry points,
   build inputs, workflow references, archived task consumers and open work.
   An index classification is a starting point, not authority over the code.
   Check for an existing local fix awaiting publication too; report that as
   pending instead of proposing duplicate work against the unchanged remote.
2. Explain the cost and benefit. Compare source and documentation weight with
   reuse, and identify a concrete retention benefit for safety, lifetime,
   platform behavior, or difficult instrumentation. An infrequently invoked
   safety instrument or self-test can still earn its permanent place.
3. Choose retain, defer, propose a smaller reference/API, or propose retirement
   in favor of task-local scaffolding. Keep active work excluded. Preserve
   useful contracts when relocating documentation; use the harness's
   `docs/adding-helpers.md` format and keep the mandatory README under 8 KiB.
4. Select a class only after its proof fits. Q0 requires demonstrated absence
   of required consumers and the normal deletion/build proof. Removing a
   still-used facility or changing its contract is not mechanical Q0; report
   the proposed alternative for the owner's decision under the applicable
   class. Never auto-delete or auto-route based solely on age or count.

Keep the normal report's two blocks. Put new decisions, held proposals and
coverage failures in **Got worse or needs a decision**. Put helper count,
source lines, mandatory/on-demand documentation weight, reuse and candidate
changes since the previous run and over 30 days in **Trends**. If an older
run lacks these metrics, say that the baseline is unavailable. Save the
inventory every day; notify the user only for meaningful changes or a newly
required decision, following the scheduled run's normal quiet rule.
