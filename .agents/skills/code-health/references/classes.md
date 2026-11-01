# Code-health task classes

A code-health task improves maintainability without changing behavior. Its
class decides what proves that, so a pure move never pays for a feature's
evidence campaign and a reshape never ships on a static reading alone.
Q0, Q1 and Q2 run perform-task's
[mechanical lane](../../perform-task/references/mechanical-lane.md): the
performer edits and proves the change itself and one fresh reviewer checks
it, from the other provider when the host can run one. Q3, Q4 and Q5 run the
full pipeline.

Every code-health `task.md` carries this section, preserved verbatim through
routing:

```markdown
## Code health

- Class: Q1
- Area: iv/editor
- Findings: large-file:Telegram/SourceFiles/iv/editor/iv_editor_state.cpp
- Metric: iv_editor_state.cpp 10963 lines -> no file over 4000
- Proof: move-only (`health.py moves`), Debug build, `health.py check` clean
```

## Rules for every class

- One concern, one revertible commit or a short series. Never mixed with
  feature work, never a bug fix. A defect found on the way becomes its own
  ordinary task; record it in the result and leave the code as it was.
- No observable behavior change: same UI, same network traffic, same stored
  formats, same logs that tests read.
- No added build work or binary weight. `health.py check` must report no new
  header-cost warning. Q0–Q2 may not introduce templates, `std::variant`
  dispatch or header-defined functions; a Q3–Q5 change that does compares
  the Debug binary's `__TEXT` size before and after (`size -m` on macOS) and
  records both.
- Net source lines go down, except for tests. A new abstraction must remove at
  least two existing copies or implement an approved invariant.
- Placement follows the nearest area map when one exists. Leave-alone entries
  in the map are out of scope.
- Test targets stay `EXCLUDE_FROM_ALL` and never link into `Telegram`.
- The task states the metric it moves, with before and after values from
  `health.py` or the build.

## Q0 — delete

Dead code, unreferenced functions, unreleased format leftovers, stale
compatibility paths, and the styles and language keys only that code used.

- Proof: the candidate list from the scan, then a Debug build and link of every
  target that compiled the code, without it, and `health.py check` with
  neither a "definition removed but still mentioned" nor a "removed its last
  use" warning. Those textual scans cover platform files this host never
  compiles, and code the Debug build's `-Wno-unused-function` hides; the
  reviewer searches platform files again. A platform-specific file is removed only when its platform's
  build can be run or the symbol is provably unreferenced by name in every
  source and generated input.
- Batch: up to ten candidates of one area in one task.
- Review: the mechanical lane's single review.

## Q1 — move

Split oversized files along the area map, move helpers out of anonymous
namespaces into shared headers, move includes from headers into `.cpp`,
forward-declare, split heavy headers.

- Proof: `health.py moves --base <task base>` reports `MOVE_ONLY`, or every
  listed line is a declaration, include, alias, or a re-wrapped signature the
  result explains. Debug build. `health.py check` clean. For header work, the
  scan's build-work total does not grow.
- Batch: the moves out of one file, or one header's include diet. Moving
  one file's code in one task keeps the window in which concurrent work must
  rebase across the move short.
- Review: the mechanical lane's single review.
- Schedule: not while an in-progress task's `task.md` names the moved files,
  nor when they changed on `origin/dev` in the last three days. Name the new
  locations in the area map so `rebase` can follow them.

## Q2 — merge duplicates

Replace copies that are identical apart from names and literal values with
one function in a file both callers' targets already link, each call site
passing its own values. Copies whose logic differs are Q4.

- Proof: Debug build, `health.py check` clean, and `health.py verify`
  reporting the clone resolved.
- Batch: one clone family.
- Review: the mechanical lane's single review.

## Q3 — pin behavior with tests

Headless characterization tests for pure logic: parsers, serializers,
converters, model operations, state machines, merge rules.

- Proof: the tests pass on unchanged product code and fail under a mutation
  check — at least five deliberate small breaks of the code under test
  (flipped comparison, dropped statement, wrong constant), each reverted after
  observing a failure. Record which mutations were caught. A test no mutation
  can fail is rejected.
- Kind-coverage tests are preferred for enum-dispatched code: run every
  enumerator through every converter, so a missing case fails a test instead
  of relying on compiler switch warnings.
- Golden data lives next to the test. Tests never require a network, account
  or running Telegram.
- Runs the full pipeline.

## Q4 — reshape

Behavior-preserving semantic refactors: extract a class or collaborator,
replace error strings with types, replace a switch with a table, split a god
object behind an unchanged façade, merge copies whose logic diverged. A
merge of diverged copies lists every difference with the decision taken and
why.

- Proof: the area's Q3 tests exist before the task starts and pass before and
  after. Where the map says behavior lives in the running client, add the
  normal targeted runtime check.
- The first Q4 task in an area requires the area map to be approved.
- Runs the full pipeline.

## Q5 — redesign

New seams, layering, ownership or interfaces.

- Precondition: the end state is approved as checkable invariants, written in
  plain language in the area map — for example "nothing under
  `iv/markdown/parse*` includes `data/`, `main/` or `history/`", or "secrets
  are read only inside `wallet/wallet_vault*`". `health.py` or a test must be
  able to verify each one.
- Design: at least two independent proposals, judged by a separate agent that
  favors deletion, fewer concepts, and an existing tdesktop mechanism (session
  component, `HistoryView::Media`, `GenericBox`, rpl producers) over a new
  one. A proposal that invents a mechanism names why no precedent fits.
- Execution: a project of Q1 and Q4 tasks, each with its own proof, plus a
  final task that turns the invariants into enforced checks.

## Trust

Q0, Q1 and Q2 are routed automatically (owner decision, 2026-10-13). Q3 and
Q4 are routed on the owner's word, Q4 also needs an approved area map, and
Q5 approved invariants. A failed audit, or a
landed code-health task later reverted or bisected as the cause of a
regression, suspends routing of its class in that area until the owner
re-enables it.
