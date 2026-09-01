---
name: perform-task
description: Resolve, start or resume, implement, review, test, and publish exactly one existing ai-tdesktop task by short slug or full dated id, including rare blocked retries and split-required results. Use when the user invokes $perform-task or /perform-task with a known task name, or when the continue scheduler delegates one selected task. Runs standard review lenses with fast applicability bailouts and selects task-specific domain and evidence instruments without selecting additional work.
---

# Perform One AI Task

When running in Grok Build, read `.grok/ai-workflow-adapter.md` completely
before any other host-specific delegation rule and apply its substitutions.

Own exactly one task through its retained change, or a proved
`already-satisfied` outcome, and a canonical AI `Approve` or exceptional
`Block`, or a canonical `Split-required` result. Do not process the inbox,
create replacement tasks, drain the queue,
select a follow-up, or consolidate pending tasks afterward. The `continue`
scheduler isolates discovery routing and queue consolidation in fresh workers
after this performer returns.

## Read the complete engine

Read these files completely before phase work:

- `references/pipeline.md` for the authoritative end-to-end runner contract;
- `references/phase-prompts.md` for exact leaf prompts and retry rules;
- `.agents/shared/test-loop.md` for the implementation/test state machine;
- `.agents/shared/build-lock-recovery.md` for bounded exact-checkout Windows
  build-lock recovery;
- `references/computer-use-testing.md` when UI-driver selection or operation is
  relevant.

The pipeline reference adapts conflicting generic test-loop mechanics for the
external AI worktree and exact-path safety. Its named adapter wins at those
points; retain every other test-loop rule.

## Resolve the workspace and task

Run from a Telegram Desktop checkout. Use the host's Python 3 command:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py resolve \
  --name <short-slug-or-full-task-id>
```

Use `python` or `py -3` when appropriate. The helper reads the ignored machine
tag, derives the checkout tag, synchronizes clean AI state, and resolves an
exact full id, exact final path slug, or exact normalized friendly title.
Prefer a unique unfinished match over approved history. Never guess among
several unfinished matches; report their full ids.

An interactive invocation requires a nonempty name. If none was supplied, ask
for the friendly short name or full id. A `continue` delegation always supplies
the full id and explicit workspace values; still resolve and verify them.

If `commits.slot_only` is nonzero and the slot is clean, run the helper's
`publish` command and resolve again. A dirty slot is valid only when every
change belongs to this checkout's one `in-progress` task. Those files are local
resumable phase state; never discard them. Any unrelated dirty or divergent
state is a hard stop.

## Acquire exactly this task

Inspect the resolved task, readiness, `other_active_task`, status, and owner.

- If another task is already `in-progress` for this checkout, stop.
- If this task is `approved`, report its completed result and stop.
- If it is `split-required`, report its published split proposal and stop. A
  direct invocation leaves routing to the human; a scheduler invocation returns
  control so `continue` can launch the dedicated split worker.
- If it is owned by another checkout, stop. Cross-checkout restart is a rare
  explicit human reassignment, never an implicit steal.
- If its dependencies are unfinished, report them and stop without starting.
- Inspect `task.md` for approved source-task prerequisites in addition to
  `depends_on`, then run `workspace.py source-lineage --task <full-task-id>`
  with one `--require <source-task-id>` for each explicit prerequisite. Require
  `current_satisfies: true` before Phase 1. For `start` or `retry`, pass the same
  `--require` arguments so claiming is machine-gated too.
- If it is `todo` and either unclaimed or owned by this checkout, atomically
  assign and activate it:

  ```bash
  python3 .agents/skills/process-inbox/scripts/workspace.py start \
    --task <full-task-id>
  ```

- If it is `blocked` and owned by this checkout, reopen it locally:

  ```bash
  python3 .agents/skills/process-inbox/scripts/workspace.py retry \
    --task <full-task-id>
  ```

  Preserve all source recovery, plans, reviews, tests, result, and evidence.
  Continue from the first incomplete validated boundary. This creates no
  `Resume` commit.
- If it is already `in-progress` and owned by this checkout, resume it without
  another state commit.

Refresh with `resolve` after each mutation. The source pipeline begins only
after the slot state shows this task `in-progress` for this checkout. For a new
task, canonical master must already contain its `Start` commit.

A source-lineage mismatch found before Phase 1 is a pre-phase routing stop, not
a task `Block`: create no phase artifacts, source edits, retained commit, or
integration task. Return the lineage report to the `continue` scheduler, which
may safely switch an existing local branch and resume. In a direct interactive
invocation, report it and ask the human. If the mismatch is first discovered
only after Phase 1 has completed, restore every owned/disposable source change
to a clean boundary and publish a genuine `blocked` result naming the exact
missing source task and appropriate branch evidence. Do not cherry-pick,
rebase, merge, or manufacture the prerequisite. This blocker is task-local;
the scheduler may continue work that does not depend on it.

## Run and publish

Execute `references/pipeline.md` exactly. A task that changes the repository
and is approved produces:

1. one or more tested source implementation-attempt commits, each with an
   exact one-line subject using the pipeline's conditional `[ai] ` prefix,
   blank line, and `Task: <full-task-id>`;
2. local tracked phase artifacts and progress in the AI slot worktree, without
   phase commits;
3. one canonical `Approve <full-task-id>` commit containing all final AI
   artifacts and state.

New and unfinished tasks use the single adaptive `implement` path. Assessment
must first confirm that the request is one cohesive implementation/review/test
unit. If it contains independently useful and independently testable product
boundaries, record `Scope: split-required` and a concrete split proposal before
source edits. The same result may arise later from the bounded convergence
assessment when the retained implementation proves that one review/evidence
campaign is not coherent. Do not force the broad request through smaller
implementation phases and call it one task. The independent assessment has
veto authority over further implementation, not authority to create, retire,
or rewrite tasks. The performer writes the split result, preserves any owned
implementation and source refs, and publishes it with
`finish --status split-required`. The checkout scheduler owns the later queue
mutation and implementation transfer. A direct invocation returns the
published proposal to the human.

For a cohesive task, use one mandatory general review, all five standard review
lenses, and a falsifiable evidence plan. On the initial implementation the
general reviewer and all lenses inspect the task and complete diff without
seeing one another's findings. A lens may return a compact
`NOT_APPLICABLE` immediately after that scan when it proves the diff affects no
mechanism it owns; otherwise it reads the relevant changed files and adjacent
code and returns `CLEAN` or `FINDINGS`. The evidence loop
may use static readings, commands and artifacts, unit tests, a standalone probe
or component binary, a Telegram Debug build with logged assertions, an in-app
overlay, Computer Use, screenshots, or any necessary combination. Do not
require a portable account, Telegram executable, or desktop unless a selected
check uses it. Do not weaken a runtime or visual check merely because another
instrument is cheaper.

The general reviewer examines every changed file in full and the evidence plan,
may reject an unsupported `NOT_APPLICABLE`, require a named domain specialist
or stronger instrument, and cannot defer its own concern. Its approval and
every clean or proved-not-applicable lens result carry forward. A fix
invalidates only the findings, changed invariants, specialists, validations,
and evidence checks it actually affects. Review fixes receive a focused general
delta review plus only those invalidated specialists; they do not restart the
full review or evidence design.

Automatic replay is bounded. If two review verdicts need changes, findings are
not converging, or a fix expands the architecture or owned paths, run the
pipeline's independent convergence assessment instead of another broad round.
It chooses a bounded focused repair, a coherent replan, or `RESCOPE_REQUIRED`;
unresolved findings are never approved merely to meet the bound. A task whose
desired outcome was already present may finish without a source commit only
after the same general review and evidence loop prove
`Outcome: already-satisfied`.

Only a genuine exhausted task blocker produces a
canonical `Block <full-task-id>` commit. Agent interruption, tool loss, and
global environment stops leave the task `in-progress` with its task-scoped
local state intact for the next invocation.

A repeated evidence setup failure is not exhausted recovery by itself. Follow
the shared directness ladder: forbid the failed command, fixture, probe, or
capture technique and make the next run closer to the changed surface. The configured
test-run cap closes one campaign: preserve prior passes, isolate the unmet
checks, and start at most one focused recovery campaign unless a fresh
assessment proves every direct strategy exhausted. A second campaign cap or a
repeated non-converging focused signature stops automatic work for an explicit
human/convergence decision; it does not start another campaign. A cap and a
`TEST_FLAW` can never by themselves publish `BLOCKED` or approval.

A pre-Runner crash or DeadlockDetector event is not an evidence setup failure merely
because the scenario did not start. Apply the shared crash diagnostics and debugger
fallback before changing an account fixture. An empty or unusable dump requires live
debugging after at most one confirmation run; it never supports a fixture verdict.

A locked macOS session is not an environment stop or evidence blocker for a
selected Telegram runtime check. Skip interactive Computer Use and complete
the same coverage through the in-binary overlay: drive the flow, log/assert,
capture widgets or windows, quit, and assess the saved artifacts. Non-app
instruments are unaffected.

A Windows build-output lock is not an immediate environment stop. Follow the
shared bounded recovery contract, including exact-path cleanup before builds.
Only its exhausted or unsafe outcome is a global hard stop; it never becomes a
task `Block`.

Do not report success from a source commit alone. The final AI commit must be
canonical. Retry ordinary concurrent-master publication races until success.
On a semantic conflict, unsafe checkout, or unreachable remote, preserve
resumable state and report a hard stop.

Return a compact result with the full task id, status or hard stop, attempts,
touched files, canonical final-publication confirmation, and exact evidence or
unverified behavior. Never persist or report commit hashes; the full task id is
the only cross-repository link.
