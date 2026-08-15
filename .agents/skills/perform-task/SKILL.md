---
name: perform-task
description: Resolve, start or resume, implement, commit, and verify exactly one existing ai-tdesktop task by short slug or full dated id, including rare blocked unfinished work. Use when the user invokes $perform-task or /perform-task with a known task name, or when the continue scheduler delegates one selected task. Runs the complete context, planning, assessment, Debug build, review, test-loop, Computer Use, recovery, and final publication pipeline without selecting additional work.
---

# Perform One AI Task

Own exactly one task through a Telegram commit and a canonical AI `Approve` or
exceptional `Block`. Do not process the inbox, split the task, drain the queue,
or select a follow-up afterward.

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
- If it is owned by another checkout, stop. Cross-checkout restart is a rare
  explicit human reassignment, never an implicit steal.
- If its dependencies are unfinished, report them and stop without starting.
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

## Run and publish

Execute `references/pipeline.md` exactly. A normal task produces:

1. one or more tested Telegram implementation-attempt commits, each with an
   exact one-line subject using the pipeline's conditional `[ai] ` prefix,
   blank line, and `Task: <full-task-id>`;
2. local tracked phase artifacts and progress in the AI slot worktree, without
   phase commits;
3. one canonical `Approve <full-task-id>` commit containing all final AI
   artifacts and state.

Read `type` from the `resolve` output before planning. A `type: verify` task
measures shipped behavior, carries no implementation, and produces **no Telegram
commit at all** — only item 2 and item 3 above. It skips implementation, the
implementation build, the four-lens review loop, and Windows normalization, and
runs the pipeline's Verification tasks profile instead: measurement plan,
falsifiability assessment, then the test loop. Its outcome is either that the
behavior held or a `Finding: deviation` recording the exact disagreement plus
follow-up tasks that repair it; both finish `approved`. Never repair what a
verification measured, and never let one commit source.

Only a genuine exhausted implementation or verification blocker produces a
canonical `Block <full-task-id>` commit. Agent interruption, tool loss, and
global environment stops leave the task `in-progress` with its task-scoped
local state intact for the next invocation.

A repeated test setup failure is not exhausted verification by itself. Follow
the shared directness ladder: forbid the failed fixture technique and make the
next run more manual and closer to the changed production seam. The configured
test-run cap is the safety boundary; the former two-identical-signature shortcut
must not be used.

A locked macOS session is not an environment stop or verification blocker.
Skip interactive Computer Use and complete the same coverage through the
in-binary overlay: drive the flow, log/assert, capture widgets or windows,
quit, and assess the saved artifacts.

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
