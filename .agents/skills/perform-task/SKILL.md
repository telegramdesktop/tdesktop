---
name: perform-task
description: Resolve, claim, resume, implement, commit, and verify exactly one existing ai-tdesktop task by short slug or full dated id, including previously blocked unfinished work. Use when the user invokes $perform-task or /perform-task with a known task name, or when the continue scheduler delegates one selected task. Runs the complete mature context, planning, assessment, Debug build, review, test-loop, Computer Use, resume, and publication pipeline without selecting any additional work.
---

# Perform One AI Task

Own exactly one task through a Telegram commit and a published attempt-boundary
AI commit. Do not
process the inbox, split the task, drain the queue, or select a follow-up after
this attempt reaches `approved` or `blocked`.

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
tag, derives the checkout tag, synchronizes clean AI state, and resolves:

1. an exact full id such as `2026/07/19/fix-community-forward`;
2. an exact final path slug such as `fix-community-forward`;
3. an exact normalized friendly task title.

Prefer a unique unfinished match when approved history has the same slug.
Never guess among several unfinished matches; report their full ids so the
human can choose one.

An interactive `/perform-task` or `$perform-task` invocation requires a
nonempty name. If none was supplied, ask for the friendly short name or full
id and do not select from the queue. A `continue` delegation always supplies
the full id.

When invoked by `continue`, accept its explicit `source_root`, `slot_worktree`,
`checkout_tag`, and full `task_id`, but still run `resolve` and verify they
match local discovery.

If `commits.slot_only` is nonzero and the slot is clean, run the helper's
`publish` command and resolve again before changing ownership. If the slot is
dirty, permit it only for an already `in-progress` task owned by this checkout
and only within that task's allowed paths; the pipeline preflight will validate
resumption ownership. Any other dirty or divergent state is a hard stop. Never
discard an unpublished checkpoint.

## Acquire exactly this task

Inspect the resolved task, its `ready` value, `other_active_task`, state, and
owner before source work.

- If another task is already `in-progress` for this checkout, stop. Never
  abandon or supersede it implicitly.
- If this task is `approved`, report its completed result and stop.
- If it is claimed by another checkout, stop without touching it.
- If it is unclaimed but its dependencies are unfinished, report those
  dependencies and stop without claiming it.
- If it is ready and unclaimed, claim only this full id:

  ```bash
  python3 .agents/skills/process-inbox/scripts/workspace.py claim \
    --task <full-task-id>
  ```

- If it is `todo` and owned by this checkout, start it:

  ```bash
  python3 .agents/skills/process-inbox/scripts/workspace.py start \
    --task <full-task-id>
  ```

- If it is `blocked` and owned by this checkout, retry it:

  ```bash
  python3 .agents/skills/process-inbox/scripts/workspace.py retry \
    --task <full-task-id>
  ```

  Preserve its claim, implementation, plans, reviews, tests, result, and
  evidence. Treat the prior blocked result as the exact resumption handoff and
  continue from the first incomplete validated boundary rather than starting
  over.

- If it is already `in-progress` and owned by this checkout, resume it without
  another claim, start, or retry commit.

Refresh with `resolve` after every state mutation. The source pipeline starts
only after canonical AI state shows this task `in-progress` for this checkout.

## Run and publish

Execute `references/pipeline.md` exactly. The task must normally produce:

1. one or more tested Telegram implementation-attempt commits, each with an
   exact one-line subject, blank line, and `Task: <full-task-id>`;
2. tracked resumable AI checkpoints during phase work;
3. an attempt-boundary AI slot commit containing final result/state, rebased and
   published to canonical AI master without force.

Do not report success from a source commit alone. The attempt-boundary AI
commit must also be canonical. On a retryable concurrent-master race, keep
fetching, rebasing, and publishing until it succeeds. On a semantic conflict,
unsafe checkout, or unreachable remote, preserve resumable state and report a
hard stop.

Return a compact result with the full task id, attempt status or hard stop,
attempts, touched files, canonical-publication confirmation, and exact evidence
or unverified behavior. Never persist or report commit hashes; the full task id
is the only cross-repository link.
