# Project bundles

Every task belongs to a project. A project is a named bundle of tasks and the
branch they are performed on, not a knowledge base: it marks the area or kind
of work its tasks share, and its branch is where their approved commits are
rebased and pushed. Existing ownership, dependency, archival, review, and
publication rules still apply.

## Document roles

- `project.md` is a `# ` title and one paragraph naming the area or kind of
  work the project bundles. Older projects carry longer overviews; they are
  history, read only for a concrete lookup, and are never extended.
- `tasks.md` is task-link navigation with concise optional grouping. Task
  `state.yaml` owns live status.
- Each task's `task.md` is self-contained, and its `work/context.md` explains
  the present request and relevant implementation facts with exact source and
  reference locations. Plans, routing reasons, decisions, and completion
  reports stay in task artifacts or receipts.

## Selective reading

Start from the current request, explicit lineage, task metadata, and relevant
source. Read the project's paragraph to know the area. Follow a dependency,
input, reference, or prior decision only when a concrete requirement or
ambiguity makes it relevant; read the needed sections and record their
locations when downstream work needs them. A link is not an instruction to
recursively load its targets.

For large legacy documents, inspect headings and search matches, then read
bounded relevant sections. Do not require complete project indexes or histories
as context. An unchanged request's read set should not grow merely because
unrelated tasks were completed.

The current task specifies the requested behavior; current source establishes
what is implemented. Check older context, plans, proposals, and snapshots
against those authorities. Historical references remain discoverable but do
not establish current behavior or override the request. Never automatically
select the latest approved task's context or accumulate earlier tasks'
decisions into a new task's context.

## No project amendments

A task never edits its project's `project.md`. A decision that should bind
later tasks is recorded in the task's `work/decisions.md`, and a later task
that depends on it names that task in `depends_on`. Legacy
`work/project-amendment.md` and `work/project.proposed.md` files are
historical artifacts: never apply or inherit them.
