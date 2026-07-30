---
name: process-inbox
description: Process the local ignored ai-tdesktop inbox into durable, independently testable Telegram Desktop task records while task execution worktrees remain active. Use when the user invokes $process-inbox or /process-inbox, asks to triage or process ai-tdesktop/inbox/inbox.md, or wants inbox notes and pasted images routed into new or existing AI projects and dated tasks without implementing them.
---

# Process Inbox

Turn the human-written ignored inbox into tracked planning artifacts. Route and
plan only: do not edit Telegram source, build, test, claim, or implement tasks.

## Workspace

Run from a Telegram Desktop checkout. Use the bundled helper with an available
Python 3 interpreter:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py inbox-ensure
python3 .agents/skills/process-inbox/scripts/workspace.py prepare
```

Use `python` or `py -3` when that is the host's Python 3 command. The helper:

- reads `Telegram/build/ai-machine-tag`;
- combines it with the checkout folder, for example `macbook-twork`;
- locates the sibling `ai-tdesktop` and `ai-tdesktop-worktrees` directories,
  with `AI_TDESKTOP_ROOT` and `AI_TDESKTOP_WORKTREES_ROOT` as overrides;
- ensures the isolated `inbox/<checkout-tag>` linked worktree exists without
  creating or inspecting the task execution worktree;
- snapshots the ignored inbox before planning and prints JSON paths.

`prepare` resumes the one active `.processing-*` snapshot when present. Save its
`transaction`, `digest`, `ai_main`, `inbox_worktree`, `inbox_branch`, and
`checkout_tag` values. Read raw input only from the transaction snapshot, not
from the live inbox.

The task execution `slot_worktree` may be dirty or have unpublished task work.
Ignore it completely while processing the inbox: do not read planning state
from it, write to it, stage it, commit it, rebase it, or publish it.

For a new transaction, `prepare` requires clean `ai_main` and `inbox_worktree`
state with no unpublished inbox commits. It fetches `origin` when configured,
fast-forwards local `master`, and fast-forwards the inbox branch before taking
the snapshot. If the inbox is empty, the machine tag is invalid, or either
inbox publication worktree is unsafe, stop without changing or clearing the
inbox. Never force-push shared AI history.

## Route and plan

Read these before planning:

- source checkout `AGENTS.md`;
- `ai_main/AGENTS.md`;
- existing `<inbox_worktree>/projects/*/project.md`, including
  `projects/archive/`, and relevant task states from `<inbox_worktree>`;
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

Bias project assignment toward continuity. When a request follows from an
existing task, begin with that task's project and keep it unless independence
is affirmatively established. A task belongs to the existing project when it
builds on project code or behavior, requires source changes shipped by project
tasks, assumes the project's branch or accumulated context, or must be ordered
after project work. Touching shared infrastructure or an additional
non-project consumer does not by itself make the task standalone: projects
record feature and code lineage, not exclusive ownership of every touched
file.

Route a derived request to another project or to `project: null` only when it
remains coherent, implementable, and independently testable in a checkout
where the originating project's changes are absent or reverted. Record that
concrete independence evidence in the receipt; "cross-cutting", "cleanup", or
"broader than the source task" is not enough. For a request without a source
task, prefer an existing project whenever its code, plan, or prior tasks supply
essential context, and use a standalone task only when no project does.

Do not create generic holding projects such as `fixes`. A release batch of
unrelated regressions normally becomes standalone tasks or tasks in existing
domain projects. Group requests into one task only when they form one cohesive,
independently testable behavior. Split work until every task is implementable
in one pass and has an exact observable acceptance result.

Project slugs are unique across `projects/` and `projects/archive/`. When a
request belongs to an archived project, restore it before routing to it:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py inbox-unarchive \
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

Write tracked planning artifacts only inside the checkout-specific
`inbox_worktree`.

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

Keep acceptance criteria to what actually proves the requested behavior. Never
write a test-data integrity criterion: the live `TelegramForcePortable` folder
is a disposable copy, so a task must not ask a performer to hash, back up,
compare, or restore the account, to verify any setting or folder is unchanged,
or to leave the account as it was found. A run may leave templates, theme,
wallpaper, window geometry and interface scale mutated. State needs restoring
only where a later measurement in the same run depends on it, never as an
end-of-run obligation. The test-loop's SETUP owns the folder — it requires the
golden `test_TelegramForcePortable`, reuses a live folder carrying the `testing`
marker, and otherwise prepares a fresh copy — and its `test-run` command already
launches with `-testagent -noupdate`, so no task needs to require either flag.
Every such criterion costs a performer real time and proves nothing about the
product.

Create `state.yaml` in this exact field order:

```yaml
status: todo
type: implement
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
of task identifiers for dependencies. Dependencies record code lineage as well
as readiness: keep an approved source task in `depends_on` when the new task's
implementation assumes its shipped changes. State that prerequisite in
`task.md`. Inbox processing never reserves work: new tasks always remain
`status: todo` with `claimed_by`, `claimed_at`, and `claim_order` set to `null`.
The checkout tag belongs in the receipt only.

Inbox processing always writes `type: implement`. A human request is work to do,
not a measurement of work already done. Only the `continue` scheduler's routing
step creates `type: verify` tasks, when an approved result leaves an
`Unverified:` gap the existing checkout could close; that step reuses these
task-creation rules and sets the field itself. A verification carries no
implementation, so never give one acceptance criteria that would need a source
change to satisfy — that request is an `implement` task.

For a new project, create `projects/<slug>/project.md` with a concise durable
scope and `projects/<slug>/tasks.md` with task links. For an existing project,
append every newly assigned task link, including inherited follow-ups. Project
indexes do not store live status.

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

Publish through the helper, passing every generated or restored task, project,
and receipt path explicitly:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py inbox-publish \
  --transaction <transaction-path> \
  --receipt <receipts/YYYY/MM/DD/name.md> \
  --path <tasks/YYYY/MM/DD/task-slug> \
  --path <projects/project-slug> \
  --path <receipts/YYYY/MM/DD/name.md>
```

Do not run broad staging commands yourself. The helper rejects changes outside
the explicit paths, stages those paths, commits them on the inbox branch with
`Process inbox for <checkout-tag>`, fetches and rebases onto current master,
pushes `HEAD:master` when an origin exists, and fast-forwards local master.
It can resume publication when the inbox commit already exists. Ordinary
non-fast-forward races are retried. If a semantic rebase conflict, unsafe
inbox worktree, or remote outage occurs, do not clear the inbox; leave the
active transaction and inbox commit recoverable and report the exact state.
Never cherry-pick or force-push, and never involve the dirty task slot.
When a project was restored, pass both `projects/<slug>` and its removed
`projects/archive/<slug>` path.

After master contains the generated commit and any configured push succeeded,
finalize using the receipt path relative to `ai_main`:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py finalize \
  --transaction <transaction-path> \
  --receipt <receipts/YYYY/MM/DD/name.md>
```

The helper accepts only a receipt below `receipts/`, verifies that exact receipt
and digest are present in local master `HEAD`, checks the live inbox digest,
preserves the raw snapshot under ignored `inbox/backup/`, and empties
`inbox.md` only when the input remained unchanged. If the human edited the
inbox during planning, it preserves those edits and reports `cleared: false`.

If planning fails before any tracked changes or commits exist, preserve the
live inbox and close the snapshot with:

```bash
python3 .agents/skills/process-inbox/scripts/workspace.py abort \
  --transaction <transaction-path>
```

Do not abort after a generated inbox commit exists; retain the transaction so
publication can be resumed.

## Report

Return a compact summary with friendly task and project titles, the AI master
publication status, the local backup path, whether the inbox was cleared, and
any unused input. Do not report a commit hash or start implementation
automatically.
