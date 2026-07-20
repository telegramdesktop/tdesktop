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
   exact one-line subject, blank line, and `Task: <full-task-id>`;
2. local tracked phase artifacts and progress in the AI slot worktree, without
   phase commits;
3. one canonical `Approve <full-task-id>` commit containing all final AI
   artifacts and state.

Only a genuine exhausted implementation or verification blocker produces a
canonical `Block <full-task-id>` commit. Agent interruption, tool loss, and
global environment stops leave the task `in-progress` with its task-scoped
local state intact for the next invocation.

Do not report success from a source commit alone. The final AI commit must be
canonical. Retry ordinary concurrent-master publication races until success.
On a semantic conflict, unsafe checkout, or unreachable remote, preserve
resumable state and report a hard stop.

Return a compact result with the full task id, status or hard stop, attempts,
touched files, canonical final-publication confirmation, and exact evidence or
unverified behavior. Never persist or report commit hashes; the full task id is
the only cross-repository link.
