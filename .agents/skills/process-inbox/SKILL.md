---
name: process-inbox
description: Process the local ignored ai-tdesktop inbox into durable, independently testable Telegram Desktop task records. Use when the user invokes $process-inbox or /process-inbox, asks to triage or process ai-tdesktop/inbox/inbox.md, or wants inbox notes and pasted images routed into new or existing AI projects and dated tasks without implementing them.
---

# Process Inbox

Turn the human-written ignored inbox into tracked planning artifacts. Route and
plan only: do not edit Telegram source, build, test, claim, or implement tasks.

## Workspace

Run from a Telegram Desktop checkout. Use the bundled helper with an available
Python 3 interpreter:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py ensure
python3 .agents/skills/process-inbox/scripts/workspace.py prepare
```

Use `python` or `py -3` when that is the host's Python 3 command. The helper:

- reads `Telegram/build/ai-machine-tag`;
- combines it with the checkout folder, for example `macbook-twork`;
- locates the sibling `ai-tdesktop` and `ai-tdesktop-worktrees` directories,
  with `AI_TDESKTOP_ROOT` and `AI_TDESKTOP_WORKTREES_ROOT` as overrides;
- ensures the local `slot/<checkout-tag>` linked worktree exists;
- snapshots the ignored inbox before planning and prints JSON paths.

`prepare` resumes the one active `.processing-*` snapshot when present. Save its
`transaction`, `digest`, `ai_main`, `slot_worktree`, `slot_branch`, and
`checkout_tag` values. Read raw input only from the transaction snapshot, not
from the live inbox.

If the inbox is empty, stop normally. If tracked AI worktrees are dirty, a slot
has unpublished commits, or the machine tag is invalid, stop without changing
or clearing the inbox.

If `ai_main` has an `origin`, fetch it before a new transaction and
fast-forward local `master` to `origin/master`. Never force-push shared AI
history.

## Route and plan

Read these before planning:

- source checkout `AGENTS.md`;
- `ai_main/AGENTS.md`;
- existing `projects/*/project.md`, including `projects/archive/`, and task
  states relevant to the request;
- the transaction's `inbox.md` and every file it references.

Use one disposable leaf planner when the harness supports delegation; instruct
it not to delegate. Otherwise perform the same work locally. The planner may
write a proposed routing file inside the ignored transaction, but only the
orchestrator writes tracked AI state.

Treat natural-language hints as evidence, not required syntax. Segment the
inbox into requests, then decide for each request whether to:

- create a standalone task with no project;
- add one or more tasks to an existing project;
- create a new project when durable shared context is useful.

Do not create generic holding projects such as `fixes`. A release batch of
unrelated regressions normally becomes standalone tasks or tasks in existing
domain projects. Group requests into one task only when they form one cohesive,
independently testable behavior. Split work until every task is implementable
in one pass and has an exact observable acceptance result.

Project slugs are unique across `projects/` and `projects/archive/`. When a
request belongs to an archived project, restore it before routing to it:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py unarchive \
  --project <slug>
```

The helper moves the project back to `projects/<slug>`, rewrites its relative
links, and leaves the restored files staged for this transaction's commit.
Never point a task at a path under `projects/archive/`.

Briefly inspect Telegram source when needed to understand scope and testable
seams. Do not plan implementation internals and do not modify the source tree.

## Assign task paths

Use the processing date and a concise imperative kebab-case slug:

```text
tasks/YYYY/MM/DD/<task-slug>/
```

The task identifier is the path below `tasks/`, for example:

```text
2026/07/18/fix-community-forward
```

Never ask the human to choose or remember it. Consult existing directories and
append `-2`, `-3`, and so on to resolve a same-day collision. Dependencies may
name only task identifiers created earlier in the same routing result or
existing tasks.

## Write tracked artifacts

Write only inside the checkout-specific `slot_worktree`.

For every task, create `task.md`:

```markdown
# <imperative title>

<self-contained request and relevant constraints>

## Acceptance

- <specific observable result proving the behavior>

## Inputs

- [<descriptive label>](input/<file>)
```

Omit `Inputs` when none are used. For visual work, include the design basis and
the exact visual/layout evidence expected. Copy every pertinent supplied file
into `input/`; never reference the ignored inbox or its backup from a task.

Create `state.yaml` in this exact field order:

```yaml
status: todo
created: YYYY-MM-DD
project: null
depends_on: []
claimed_by: null
claimed_at: null
claim_order: null
lease_until: null
phase: null
inbox_receipt: receipts/YYYY/MM/DD/<receipt>.md
```

Use a project slug instead of `null` when routed to a project. Use a YAML list
of task identifiers for dependencies. Inbox processing never reserves work:
new tasks always remain `status: todo` with `claimed_by`, `claimed_at`, and
`claim_order` set to `null`. The checkout tag belongs in the receipt only.

For a new project, create `projects/<slug>/project.md` with a concise durable
scope and `projects/<slug>/tasks.md` with task links. For an existing project,
append only new links. Project indexes do not store live status.

Create one tracked Markdown receipt under `receipts/YYYY/MM/DD/`. Include:

- local processing time, checkout tag, and inbox digest;
- every inbox request mapped to friendly task titles and identifiers;
- every supplied file mapped to its copied task input, or explicitly unused;
- created projects and updated projects;
- deduplication decisions.

Before writing, search receipts for the same digest. If it was already fully
processed and all referenced tasks still exist, create nothing and reuse that
receipt for finalization.

## Validate and publish

Before committing, verify:

- every request and supplied file is accounted for;
- every new task has `task.md`, valid `state.yaml`, and a falsifiable
  acceptance result;
- every task link, dependency, and copied input exists;
- no task or project reference points into `projects/archive/`;
- no raw inbox path, `.local/`, browser profile, portable account, credential,
  complete run directory, or complete build log is tracked;
- no Telegram or AI commit hash is copied into a task, project, or receipt;
- only expected `tasks/`, `projects/`, and `receipts/` paths changed;
- tracked text uses the checkout's native convention (LF on Unix/WSL, CRLF on
  native Windows) without a BOM or mixed line endings.

Stage only explicit generated paths; never use `git add -A`. Commit on the slot
branch with the one-line subject:

```text
Process inbox for <checkout-tag>
```

Immediately before publishing, fetch `origin` when configured and
fast-forward `ai_main/master` when possible. Rebase the clean slot branch onto
local `master`, then fast-forward `master` to the slot branch. Push `master`
when an origin exists. Retry ordinary non-fast-forward races by fetching,
rebasing, and publishing again until success. If a semantic rebase conflict,
unsafe worktree, or remote outage occurs, do not clear the inbox; leave the
active transaction and slot commit recoverable and report the exact state.
Never force-push.

After master contains the generated commit and any configured push succeeded,
finalize using the receipt path relative to `ai_main`:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py finalize \
  --transaction <transaction-path> \
  --receipt <receipts/YYYY/MM/DD/name.md>
```

The helper verifies the live inbox digest, preserves the raw snapshot under
ignored `inbox/backup/`, and empties `inbox.md` only when the input remained
unchanged. If the human edited the inbox during planning, it preserves those
edits and reports `cleared: false`.

If planning fails before any tracked changes or commits exist, preserve the
live inbox and close the snapshot with:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py abort \
  --transaction <transaction-path>
```

Do not abort after a generated commit exists; retain the transaction so the
publication can be resumed.

## Report

Return a compact summary with friendly task and project titles, the AI master
publication status, the local backup path, whether the inbox was cleared, and
any unused input. Do not report a commit hash or start implementation
automatically.
