# Compact project context

Projects preserve useful feature and code lineage across tasks, including small
projects. Keep that continuity; context growth is not a reason to fork a project
or create a fresh project for each task. Existing ownership, dependency,
archival, review, and publication rules still apply.

## Document roles

- `project.md` is a small durable overview: scope, constraints, key boundaries,
  source locators, and links to detailed references. It is not a complete
  implementation blueprint, plan, or task history.
- The current task's `work/context.md` explains the present request and relevant
  implementation facts, with exact source/reference locations. Keep enough
  context for the next phase without copying the project's accumulated history.
- `tasks.md` is task-link navigation with concise optional grouping. Task
  `state.yaml` owns live status. Put plans, routing reasons, decisions, and
  completion reports in task artifacts or receipts, not in the index.

## Selective reading

Start from the current request, explicit lineage, task metadata, and relevant
source. Use project names/titles and targeted search to select candidate small
overviews. Follow a dependency, input, reference, or prior decision when a
concrete requirement or ambiguity makes it relevant; read the needed sections
and record their locations when downstream work needs them. A link is not an
instruction to recursively load its targets.

For large legacy documents, inspect headings and search matches, then read
bounded relevant sections. Do not require complete project indexes or histories
as context. An unchanged request's read set should not grow merely because
unrelated tasks were completed.

The current task specifies the requested behavior; current source establishes
what is implemented. Check older context, plans, proposals, and snapshots
against those authorities. Historical references remain discoverable but do
not establish current behavior or override the request. Never automatically
select the latest approved task's context or accumulate earlier amendments
into a new task's context or proposal.

## Optional durable amendments

Only when the task changes a useful shared fact, write
`work/project-amendment.md` with the exact target section of its project's
`project.md`, the proposed narrow change, why the fact is durable and useful,
and its supporting task/source reference. No amendment or empty placeholder is
required for routine completion. Keep implementation detail in the task.

An amendment is a task-local proposal until final approval. After review and
evidence approve the outcome, reread the latest canonical target section,
reconcile concurrent edits, and apply only the still-valid narrow change while
preserving unrelated text. Skip an amendment that is already covered or no
longer useful. Review a material reconciliation before publication. If only the
optional note cannot be reconciled, leave it task-local and finish without it;
stop when a conflict invalidates task correctness or actual publication fails.
Blocked or split-required work retains proposals only in the task and publishes
no proposed project facts.

On resume, legacy `work/project.proposed.md` files are historical artifacts,
never whole-document replacements. If a still-useful fact survives verification
against the current task and source, express just that fact as a fresh narrow
amendment. Do not inherit a proposal chain. Apply amendments through the
workflow's existing publication boundary and exact path scope.
